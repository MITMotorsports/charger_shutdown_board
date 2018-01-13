#include "charge.h"

static uint16_t total_num_cells;
static uint32_t cc_charge_voltage_mV;
static uint32_t cc_charge_current_mA;
static uint32_t cv_charge_voltage_mV;
static uint32_t cv_charge_current_mA;
static uint32_t last_time_above_cv_min_curr;

void _set_output(bool close_contactors, bool charger_on, uint32_t charge_voltage_mV, uint32_t charge_current_mA, CSB_OUTPUT_T *output);

void Charge_Init(CSB_STATE_T *state) {
    state->charge_state = CSB_CHARGE_OFF;
    last_time_above_cv_min_curr = 0;
}

void Charge_Config(PACK_CONFIG_T *pack_config) {
    total_num_cells = pack_config->num_modules * pack_config->module_cell_count;

    cc_charge_voltage_mV = pack_config->cc_cell_voltage_mV * total_num_cells;
    cc_charge_current_mA = pack_config->cell_capacity_cAh * pack_config->cell_charge_c_rating_cC * pack_config->pack_cells_p / 10;

    cv_charge_voltage_mV = pack_config->cell_max_mV * total_num_cells;
    cv_charge_current_mA = cc_charge_current_mA;
}

void Charge_Step(CSB_INPUT_T *input, CSB_STATE_T *state, CSB_OUTPUT_T *output) {

    switch (input->mode_request) {
        case CSB_SSM_MODE_CHARGE:
            if (state->charge_state == CSB_CHARGE_OFF
                    || state->charge_state == CSB_CHARGE_BAL) {
                state->charge_state = CSB_CHARGE_INIT;
            }
            break;

        case CSB_SSM_MODE_BALANCE:
            if (state->charge_state == CSB_CHARGE_OFF
                    || state->charge_state == CSB_CHARGE_CC
                    || state->charge_state == CSB_CHARGE_CV) {
                state->charge_state = CSB_CHARGE_INIT;
            }
            break;

        // we want to switch states (either to STANDBY/DISCHARGE/ERROR)
        default:
            if(state->charge_state == CSB_CHARGE_OFF) {
                state->charge_state = CSB_CHARGE_OFF;
            } else {
                state->charge_state = CSB_CHARGE_DONE;
            }
            break;
    }

    switch (state->charge_state) {
        case CSB_CHARGE_OFF:
            _set_output(false, false, 0, 0, output);
            break;
        case CSB_CHARGE_INIT:
            _set_output((input->mode_request == CSB_SSM_MODE_CHARGE), false, 0, 0, output);

            if (input->contactors_closed == output->close_contactors) {
                if(input->mode_request == CSB_SSM_MODE_CHARGE) {
                    state->charge_state =
                        (input->pack_status->pack_cell_max_mV < state->pack_config->cell_max_mV) ? CSB_CHARGE_CC : CSB_CHARGE_CV;
                } else if (input->mode_request == CSB_SSM_MODE_BALANCE) {
                    state->charge_state = CSB_CHARGE_BAL;
                }
            }
            break;
        case CSB_CHARGE_CC:
            if (input->pack_status->pack_cell_max_mV >= state->pack_config->cell_max_mV) {
                state->charge_state = CSB_CHARGE_CV; // Need to go to CV Mode
                _set_output(true, true, cv_charge_voltage_mV, cv_charge_current_mA, output);
            } else {
                // Charge in CC Mode
                _set_output(true, true, cc_charge_voltage_mV, cc_charge_current_mA, output);
            }

            // if(!input->contactors_closed || !input->charger_on) { // [TODO] Think about this
            if(!input->contactors_closed) {
                _set_output(true, false, 0, 0, output);
                state->charge_state = CSB_CHARGE_INIT;
            }
            break;
        case CSB_CHARGE_CV:

            if (input->pack_status->pack_cell_max_mV < state->pack_config->cell_max_mV) {
                // Need to go back to CC Mode
                state->charge_state = CSB_CHARGE_CC;
                _set_output(true, true, cc_charge_voltage_mV, cc_charge_current_mA, output);
            } else {
                _set_output(true, true, cv_charge_voltage_mV, cv_charge_current_mA, output);

                if (input->pack_status->pack_current_mA < state->pack_config->cv_min_current_mA*state->pack_config->pack_cells_p) {
                    if ((input->msTicks - last_time_above_cv_min_curr) >= state->pack_config->cv_min_current_ms) {
                        _set_output(false, false, 0, 0, output);
                        state->charge_state = CSB_CHARGE_DONE;
                        break;
                    }
                } else {
                    last_time_above_cv_min_curr = input->msTicks;
                }
            }

            if(!input->contactors_closed) {
                _set_output(true, false, 0, 0, output);
                state->charge_state = CSB_CHARGE_INIT;
            }
            break;

        case CSB_CHARGE_BAL:
            _set_output(false, false, 0, 0, output);
            bool balancing = input->balance_req;

            // Done balancing
            if (!balancing) {
                state->charge_state = CSB_CHARGE_DONE;
            }

            if(input->contactors_closed) {
                _set_output(false, false, 0, 0, output);
                state->charge_state = CSB_CHARGE_INIT;
            }

            break;
        case CSB_CHARGE_DONE:
            _set_output(false, false, 0, 0, output);

            // if not in Charge or Balance, that means SSM is trying to switch to another mode so wait for contactors to close
            // if in charge or balance, make sure we don't need to go back to charge or balance
            //    if we do, go back to init
            if (input->mode_request != CSB_SSM_MODE_CHARGE && input->mode_request != CSB_SSM_MODE_BALANCE) {
                if (!input->contactors_closed && !input->charger_on) {
                    state->charge_state = CSB_CHARGE_OFF;
                }
            } else {
                if(input->mode_request == CSB_SSM_MODE_CHARGE) {
                    if (input->pack_status->pack_cell_max_mV < state->pack_config->cell_max_mV) {
                        state->charge_state = CSB_CHARGE_INIT;
                    }
                } else if (input->mode_request == CSB_SSM_MODE_BALANCE) {
                    int i;
                    for (i = 0; i < total_num_cells; i++) {
                        if (input->pack_status->cell_voltages_mV[i] > input->balance_mV + state->pack_config->bal_on_thresh_mV) {
                            state->charge_state = CSB_CHARGE_INIT;
                        }
                    }
                }
            }
    }
}

void _set_output(bool close_contactors, bool charger_on, uint32_t charge_voltage_mV, uint32_t charge_current_mA, CSB_OUTPUT_T *output) {
    output->close_contactors = close_contactors;
    output->charger_on = charger_on;
    output->voltage_req_mV = charge_voltage_mV;
    output->current_req_mA = charge_current_mA;
}
