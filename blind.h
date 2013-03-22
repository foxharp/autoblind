
void blind_init(void);
void blind_ir(void);
void blind_process(void);

enum {
    BL_STOP = 1,
    BL_GO_UP,
    BL_GO_DOWN,
    BL_SET_TOP,
    BL_SET_BOTTOM,
    BL_FORCE_UP,
    BL_FORCE_DOWN,
};

extern char blind_cmd;

