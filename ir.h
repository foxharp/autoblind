extern word ir_code;
extern byte ir_i;

void ir_process(void);
void ir_show_code(void);
void ir_init(void);
char get_ir(void);

extern char ir_code_avail;
#define ir_avail()  (ir_code_avail)
