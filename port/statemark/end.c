/*
 * Save states: the last object on the link line of every played build (begin.c says why). Empty in every
 * test build.
 */
#if defined(PORT_RFO_STATE)
unsigned char gPortStateDataEnd[4] = { '>', '8', 'E', 'F' };
unsigned char gPortStateBssEnd[4];
#ifdef _WIN32
unsigned char gPortStateData2End[4] __attribute__((section(".data$zzzzzzzzfe8pc_state_end"))) = { ']', '8', 'E', 'F' };
#endif
#endif
