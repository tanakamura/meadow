
#include <windows.h>

extern DWORD block_input_ownthread;
extern CRITICAL_SECTION critsec_message;
extern CRITICAL_SECTION critsec_access_event;
extern HANDLE next_message_block_event;
extern HANDLE keyboard_handle;
extern HANDLE interrupt_handle;
extern DWORD main_thread_id;

#define W32_BLOCK_EVENT EnterCriticalSection(&critsec_access_event)
#define W32_UNBLOCK_EVENT LeaveCriticalSection(&critsec_access_event)

#define W32_SPECIAL_INPUT w32_special_input = 1
#define W32_SPECIAL_INPUT_END w32_special_input = 0
