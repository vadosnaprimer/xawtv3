extern int ftp_connected;
extern int ftp_debug;

void ftp_init(int autologin, int passive);
void ftp_send(int argc, ...);
int  ftp_recv(void);
void ftp_connect(char *host, char *user, char *pass, char *dir);
void ftp_upload(char *local, char *remote, char *tmp);
