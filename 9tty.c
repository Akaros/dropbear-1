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
	int mastertome[2], metoslave[2];
	int pid = 0;
	int i, j;

	/* Plan 9 pipes are cross-connected and bidi. Very different, much better than
	 * unix pipes. We could not do this with unix pipes at all.
	 * other end always reads/writes [0], we read/write [1] */
	pipe(mastertome); // dropbear writes to [0], we write to [1]
	pipe(metoslave); // process writes to [0], we write to [1]
	snprintf(namebuf, namebuflen, "pipe");

	static char buf[512];
	static char obuf[512];
	int nfr, nto;

	*ptyfd = metoslave[0];
	*ttyfd = mastertome[0];
	TRACE(("ptyfd %d, ttyfd %d\n", metoslave[0], mastertome[1]));
	/* at some future date, we'll implement tty handling here unless we want to do as in harvey
	 * and fire up a 9p server for /dev/tty (also a possibility, that worked well) */
HERE;
	/* from child to parent. Very little interpreation. */
	switch(fork()) {
	case -1:
		return 0;
	case 0:
HERE;
		while((nfr = read(metoslave[1], buf, 1 /*sizeof buf*/)) > 0){
			int i, j;
			j = 0;
			for(i = 0; i < nfr; i++){
				if(buf[i] == '\n'){
					if(j > 0){
						write(mastertome[1], buf, j);
						j = 0;
					}
					write(mastertome[1], "\r\n", 2);
				} else {
					buf[j++] = buf[i];
				} 
			}

			if(write(mastertome[1], buf, j) != j)
				sysfatal("write");
		}
		fprintf(stdout, "----------------------------->>>>>>>>>>>>>>>>>>>>>>>>>>>>aux/tty: got eof\n");
		// not yet.
		//postnote(PNPROC, getppid(), "interrupt");
		close(metoslave[1]);
		close(mastertome[1]);
		sysfatal("eof");
	}

	/* from parent to child. All kinds of tty handling. */
	switch(fork()) {
	case -1:
		return 0;
	case 0:
		j = 0;
		while((nto = read(mastertome[1], buf, 1 /*sizeof buf*/)) > 0){
			int oldj;
			oldj = j;
			for(i = 0; i < nto; i++){
				if(buf[i] == '\r' || buf[i] == '\n'){
					obuf[j++] = '\n';
					write(metoslave[1], obuf, j);
					if(echo){
						obuf[j-1] = '\r';
						obuf[j++] = '\n';
						write(metoslave[1], obuf+oldj, j-oldj);
					}
					j = 0;
				} else if(buf[i] == '\003'){ // ctrl-c
					if(j > 0){
						write(metoslave[1], obuf, j);
						j = 0;
					}
					fprintf(stdout, "aux/tty: NOT sent interrupt to %d\n", pid);
					//			postnote(PNGROUP, pid, "interrupt");
					continue;
				} else if(buf[i] == '\004'){ // ctrl-d
					if(j > 0){
						if(echo)write(1, obuf+oldj, j-oldj);
						write(metoslave[1], obuf, j);
						j = 0;
					}
					fprintf(stdout, "aux/tty: NOT sent eof to %d\n", pid);
					write(metoslave[1], obuf, 0); //eof
					continue;
				} else if(buf[i] == 0x15){ // ctrl-u
					if(!raw){
						while(j > 0){
							j--;
							write(metoslave[1], "\x15", 1); 
						}
					} else {
						obuf[j++] = buf[i];
					}
					continue;
				} else if(buf[i] == 0x7f || buf[i] == '\b'){ // backspace
					if(!raw){
						if(j > 0){
							j--;
							write(metoslave[1], "\b", 1);
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
					write(metoslave[1], obuf, j);
					j = 0;
				} else if(echo && j > oldj){
					write(metoslave[1], "\b", 1);
				}

			}
		}
		close(metoslave[1]);
		close(mastertome[1]);
		printf("----------------------->>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>all done");
		sysfatal("----------------------->>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>all done");
	}
	return 1;
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
