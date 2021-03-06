#include <string.h>
#include <stdlib.h>
#include "structs.h"
#include "config.h"
#include "ssm.h"
#include "board.h"
#include "console.h"
#include "pack_config.h"

volatile uint32_t msTicks;

static char str[10];

// memory allocation for CBS_OUTPUT_T
static bool balance_waitingoff[MAX_NUM_MODULES*MAX_CELLS_PER_MODULE];
static uint32_t balance_timeon[MAX_NUM_MODULES*MAX_CELLS_PER_MODULE];
static CSB_OUTPUT_T csb_output;

// memory allocation for BMS_INPUT_T
static BMS_PACK_STATUS_T pack_status;
static ELCON_STATUS_T elcon_status;
static CSB_INPUT_T csb_input;

// memory allocation for BMS_STATE_T
static PACK_CONFIG_T pack_config;
static CSB_STATE_T csb_state;

// memory for console
static microrl_t rl;
static CONSOLE_OUTPUT_T console_output;

void Init_Structs(void) {
  csb_output.voltage_req_mV = 0;
  csb_output.current_req_mA = 0;
  csb_output.send_bms_config = false;
  csb_output.close_contactors = false;
  csb_output.charger_on = false;

  csb_state.pack_config = &pack_config;
  csb_state.curr_mode = CSB_SSM_MODE_INIT;
  csb_state.init_state = CSB_INIT_OFF;
  csb_state.charge_state = CSB_CHARGE_OFF;
  csb_state.idle_state = CSB_IDLE_OFF;
  csb_state.curr_baud_rate = BMS_CAN_BAUD;
  csb_state.balance_waitingoff = balance_waitingoff;
  memset(balance_waitingoff, 0, sizeof(balance_waitingoff[0])*MAX_NUM_MODULES*MAX_CELLS_PER_MODULE);
  csb_state.balance_timeon = balance_timeon;
  memset(balance_timeon, 0, sizeof(balance_timeon[0])*MAX_NUM_MODULES*MAX_CELLS_PER_MODULE);

  pack_config.module_cell_count = 0;
  pack_config.cell_min_mV = 0;
  pack_config.cell_max_mV = 0;
  pack_config.cell_capacity_cAh = 0;
  pack_config.num_modules = 0;
  pack_config.cell_charge_c_rating_cC = 0;
  pack_config.bal_on_thresh_mV = 0;
  pack_config.bal_off_thresh_mV = 0;
  pack_config.pack_cells_p = 0;
  pack_config.cv_min_current_mA = 0;
  pack_config.cv_min_current_ms = 0;
  pack_config.cc_cell_voltage_mV = 0;

  //assign csb_inputs
  csb_input.mode_request = CSB_SSM_MODE_IDLE;
  csb_input.balance_mV = 0; // console request balance to mV
  csb_input.msTicks = msTicks;
  csb_input.elcon_status = &elcon_status;
  csb_input.pack_status = &pack_status;
  csb_input.balance_req = false;
  csb_input.contactors_closed = false;
  csb_input.receive_bms_config = false;
  csb_input.imd_fault = false;
  csb_input.int_fault = false;
  csb_input.bms_fault = false;
  csb_input.low_side_cntr_fault = false;
  csb_input.bms_error = CAN_BMSERRORS_NONE;

  pack_status.pack_cell_max_mV = 0;
  pack_status.pack_current_mA = 0;
  pack_status.pack_voltage_mV = 0;

  elcon_status.elcon_output_voltage = 0;
  elcon_status.elcon_output_current = 0;
  elcon_status.elcon_has_hardware_failure = false;
  elcon_status.elcon_over_temp_protection_on = false;
  elcon_status.elcon_is_input_voltage_wrong = false;
  elcon_status.elcon_battery_voltage_not_detected = false;
  elcon_status.elcon_on = false;
  elcon_status.elcon_charging = false;
}

void Process_Output(CSB_INPUT_T* csb_input, CSB_OUTPUT_T* csb_output, CSB_STATE_T* csb_state) {
  Board_Contactors_Set(csb_output->close_contactors);
  Board_Can_ProcessOutput(csb_input, csb_state, csb_output);
}

void Process_Input(CSB_INPUT_T* csb_input, CSB_STATE_T* csb_state) {
  Board_Can_ProcessInput(csb_input, csb_state);
  Board_GetModeRequest(&console_output, csb_input, csb_state);
  csb_input->msTicks = msTicks;
  csb_input->contactors_closed = Board_Contactors_Closed();
  Board_Check_Faults(csb_input);
}

void Process_Keyboard(void) {
    uint32_t readln = Board_Read(str,50);
    uint32_t i;
    for(i = 0; i < readln; i++) {
        microrl_insert_char(&rl, str[i]);
    }
}

int main(void) {
  Init_Structs();
  Board_UART_Init(UART_BAUD);
  Board_Chip_Init();
  Board_GPIO_Init();

  MY18_Pack_Config(&csb_state);
  SSM_Init(&csb_state);
  Board_Can_Init(CSB_CAN_BAUD);

  //setup readline
  microrl_init(&rl, Board_Print);
  microrl_set_execute_callback(&rl, executerl);
  console_init(&csb_input, &csb_state, &console_output);

  while(1) {
    Process_Keyboard();
    Process_Input(&csb_input, &csb_state);
    SSM_Step(&csb_input, &csb_state, &csb_output);
    Process_Output(&csb_input, &csb_output, &csb_state);
    Output_Measurements(&console_output, &csb_input, msTicks);
  }
}
