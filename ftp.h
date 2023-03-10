#ifndef ATHERNET_FTP_H
#define ATHERNET_FTP_H

#include <signal.h>
#include <stddef.h>

extern volatile sig_atomic_t ftp_input_stopped;

void *ftp_input_loop(void *args);
void ftp_prepare_cmd(void);
void ftp_handle_reply(size_t reply_len);

#endif // ATHERNET_FTP_H
