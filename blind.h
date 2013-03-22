
void blind_init(void);
void blind_ir(void);
void blind_process(void);

#define BL_STOP 1
#define BL_GO_UP 2
#define BL_GO_DOWN 3
#define BL_SET_TOP 4
#define BL_FORCE_UP 5
#define BL_SET_BOTTOM 6
#define BL_FORCE_DOWN 7

extern char blind_cmd;

