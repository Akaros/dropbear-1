/*
 * This file is part of the UCB release of Plan 9. It is subject to the license
 * terms in the LICENSE file found in the top-level directory of this
 * distribution and at http://akaros.cs.berkeley.edu/files/Plan9License. No
 * part of the UCB release of Plan 9, including this file, may be copied,
 * modified, propagated, or distributed except according to the terms contained
 * in the LICENSE file.
 */

#include "includes.h"
#include "dbutil.h"
#include "errno.h"
#include "sshpty.h"

int echo = 1;
int raw = 0;

#define sysfatal(f...) {fprintf(stderr, f); exit(1); }

/*
 * Allocates and opens a pty.  Returns 0 if no pty could be allocated, or
 * nonzero if a pty was successfully allocated.  On success, open file
 * descriptors for the pty and tty sides and the name of the tty side are
 * returned (the buffer must be able to hold at least 64 characters).
 *
 * in this version, since we don't have ptys in the kernel, and hopefully never will, we kick
 * off a proc to act like a pty. On Harvey, we export a name space that servers /dev/cons and
 * /dev/consctl and we might consider doing that here too.
 * Plan 9 pipes are bidi, i.e. pipe returns two bidirectional fds, not one one-way fd. This matches what
 * the caller expects in ptyfd and ttyfd. In this case, the ptyfd will be the input to the child process,
 * and the ttyfd will be the output that ssh will then make the input to the child shell. Are you confused yet?
 * the purpose of this is to support the kinds of things that ptys do on unix. 
 */

int
pty_allocate(int *ptyfd, int *ttyfd, char *namebuf, int namebuflen)
{
	int process[2];
	int pid = 0;
	int i, j;

	pipe(process); // pty will be [0], tty will be [1]; it actually does not matter.
	snprintf(namebuf, namebuflen, "pipe");

	static char buf[512];
	static char obuf[512];
	int nfr, nto;

	/* read from process[0], write to [1]
	 * read from process[1], write to process[0]
	 */
	*ptyfd = process[0];
	*ttyfd = process[1];
	/* from child to parent. Very little interpreation. */
	switch(fork()) {
	case -1:
		return -1;
	case 0:
		while((nfr = read(process[1], buf, sizeof buf)) > 0){
if (echo) write(1, buf, nfr);
			int i, j;
			j = 0;
			for(i = 0; i < nfr; i++){
				if(buf[i] == '\n'){
					if(j > 0){
						write(process[0], buf, j);
						j = 0;
					}
					write(process[0], "\r\n", 2);
				} else {
					buf[j++] = buf[i];
				} 
			}

			if(write(process[0], buf, j) != j)
				sysfatal("write");
		}
		fprintf(stdout, "aux/tty: got eof\n");
		// not yet.
		//postnote(PNPROC, getppid(), "interrupt");
		sysfatal("eof");
	}

	/* from parent to child. All kinds of tty handling. */
	switch(fork()) {
	case -1:
		return -1;
	case 0:
		j = 0;
		while((nto = read(process[0], buf, sizeof buf)) > 0){
			int oldj;
			oldj = j;
			for(i = 0; i < nto; i++){
				if(buf[i] == '\r' || buf[i] == '\n'){
					obuf[j++] = '\n';
					write(process[1], obuf, j);
					if(echo){
						obuf[j-1] = '\r';
						obuf[j++] = '\n';
						write(1/*process[1]*/, obuf+oldj, j-oldj);
					}
					j = 0;
				} else if(buf[i] == '\003'){ // ctrl-c
					if(j > 0){
						if(echo)write(1/*process[1]*/, obuf+oldj, j-oldj);
						write(process[1], obuf, j);
						j = 0;
					}
					fprintf(stdout, "aux/tty: NOT sent interrupt to %d\n", pid);
					//			postnote(PNGROUP, pid, "interrupt");
					continue;
				} else if(buf[i] == '\004'){ // ctrl-d
					if(j > 0){
						if(echo)write(1, obuf+oldj, j-oldj);
						write(process[1], obuf, j);
						j = 0;
					}
					fprintf(stdout, "aux/tty: NOT sent eof to %d\n", pid);
					write(process[1], obuf, 0); //eof
					continue;
				} else if(buf[i] == 0x15){ // ctrl-u
					if(!raw){
						while(j > 0){
							j--;
							if(echo)write(1, "\b \b", 3); // bs
							else write(process[1], "\x15", 1); // bs
						}
					} else {
						obuf[j++] = buf[i];
					}
					continue;
				} else if(buf[i] == 0x7f || buf[i] == '\b'){ // backspace
					if(!raw){
						if(j > 0){
							j--;
							if(echo)write(1, "\b \b", 3); // bs
							else write(process[1], "\b", 1); // bs
						}
					} else {
						obuf[j++] = '\b';
					}
					continue;
				} else {
					obuf[j++] = buf[i];
				}
			}
			if(j > 0){
				if(raw){
					if(echo)write(1, obuf, j);
					write(process[1], obuf, j);
					j = 0;
				} else if(echo && j > oldj){
					write(1, obuf+oldj, j-oldj);
				}

			}
		}
		close(process[1]);
	}
	sysfatal("all done");
}

void
pty_change_window_size(int ptyfd, int row, int col,
	int xpixel, int ypixel)
{
	fprintf(stderr, "%s(%d, %d, %d, %d, %d): not yet\n", __func__, ptyfd, row, col,xpixel, ypixel);
}

void
pty_release(const char *tty_name)
{
	fprintf(stderr, "%s %s: not yet\n", __func__, tty_name);
}

void
pty_make_controlling_tty(int *ttyfd, const char *tty_name)
{
	fprintf(stderr, "%s %d %s: not yet\n", __func__, *ttyfd, tty_name);
}
