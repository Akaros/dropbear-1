/* Copyright (c) 2016 Google, Inc.
 * Barret Rhoden <brho@cs.berkeley.edu>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors
 * may be used to endorse or promote products derived from this software without
 * specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE. */

#include "includes.h"
#include "dbutil.h"
#include "errno.h"
#include "sshpty.h"

#include <pthread.h>
#include <parlib/parlib.h>

#define handle_error(msg) \
        do { perror(msg); exit(-1); } while (0)

/* Global FDs and pthreads */
static int intr_parent;
static int intr_child;
static pthread_t child_to_parent;
static pthread_t parent_to_child;

struct interp_rule {
	char 						chr;
	int (*func)(int to_fd);
};

/* Helper: looks up interp rules.  Note the last entry has a null function
 * pointer. */
static struct interp_rule *lookup_interp(struct interp_rule *rules, char c)
{
	for (struct interp_rule *i = rules; i->func; i++) {
		if (i->chr == c)
			return i;
	}
	return 0;
}

/* Generic function that interprets data flowing from_fd -> to_fd, subject to
 * rules.  You provide the rules that match chars to funcs.  The funcs emulate
 * whatever the char was, and return how much of the input stream we should jump
 * over (usually just the one char).
 *
 * As we add more rules, we might need more info/capabilities in the rules.  For
 * instance, we might want to throw out older data (which means we need to
 * buffer until we get a \n).  Feel free to change this up.  =) */
static void *intr_data_flow(int from_fd, int to_fd, struct interp_rule *rules)
{
	char buf[512];
	char *last_write_pos;
	size_t amt_read, amt_write;
	int ret;
	struct interp_rule *rule;

	while (1) {
		amt_read = read(from_fd, buf, sizeof(buf));
		if (amt_read < 0)
			handle_error("data_flow read");
		if (amt_read == 0)
			break;
		last_write_pos = buf;
		for (int i = 0; i < amt_read; i++) {
			rule = lookup_interp(rules, buf[i]);
			if (rule) {
				/* write up to but not including buf[i], from last */
				amt_write = &buf[i] - last_write_pos;
				ret = write(to_fd, last_write_pos, amt_write);
				assert(ret == amt_write);
				ret = rule->func(to_fd);
				/* skip over whatever they interpreted */
				last_write_pos = &buf[i] + ret;
			}
		}
		if (last_write_pos < &buf[amt_read]) {
			amt_write = &buf[amt_read] - last_write_pos;
			ret = write(to_fd, last_write_pos, &buf[amt_read] - last_write_pos);
			assert(ret == amt_write);
		}
	}
}

static int child_slash_n(int to_fd)
{
	int ret;

	ret = write(to_fd, "\r\n", 2);
	assert(ret == 2);
	return 1;
}

static struct interp_rule child_rules[] = 
{
	{ '\n', child_slash_n },
	{ '\0', 0 },
};

/* Helper pthread, interprets data flowing from child to parent.
 *
 * ptyfd <- intr_parent: intr_child_to_parent() : intr_child <- ttyfd */
static void *intr_child_to_parent(void *arg)
{
	intr_data_flow(intr_child, intr_parent, child_rules);
}

/* Kill a decescendent, grandchild or younger. */
static int parent_ctrl_c(int to_fd)
{
	int ret, killkid_fd;

	killkid_fd = open("#cons/killkid", O_WRONLY);
	if (killkid_fd >= 0) {
		if (write(killkid_fd, "killkid", 7) < 7) {
			dropbear_log(LOG_ERR, "write consctl killkid: %r");
		}
		close(killkid_fd);
	} else {
		dropbear_log(LOG_ERR, "Open #cons/consctl: %r");
	}
	/* TODO: we should flush the existing input data, but for now we'll just
	 * enter it.  Best of luck. */
	ret = write(to_fd, "\n", 1);
	assert(ret == 1);
	return 1;
}

static struct interp_rule parent_rules[] = 
{
	{ '\003', parent_ctrl_c },
	{ '\0', 0 },
};

/* Helper pthread, interprets data flowing from parent to child.
 *
 * ptyfd -> intr_parent: intr_parent_to_child() : intr_child -> ttyfd */
static void *intr_parent_to_child(void *arg)
{
	intr_data_flow(intr_parent, intr_child, parent_rules);
}

/* Allocates and opens a pty.  Returns 0 if no pty could be allocated, or
 * nonzero if a pty was successfully allocated.  On success, open file
 * descriptors for the pty and tty sides and the name of the tty side are
 * returned (the buffer must be able to hold at least 64 characters). */
int pty_allocate(int *ptyfd, int *ttyfd, char *namebuf, int namebuflen)
{
	int child_side[2];
	int parent_side[2];
	int pid = 0;
	int i, j;

	/* Pipe between the child (ttyfd) and interpreter.  Child gets [1]. */
	if (pipe(child_side))
		handle_error("pipe child_side");
	/* Pipe between the parent (ptyfd) and interpreter.  Parent gets [0]. */
	if (pipe(parent_side))
		handle_error("pipe parent_side");
	/* ptyfd <-> intr_parent {Our threads} intr_child <-> ttyfd */
	*ptyfd = parent_side[0];
	intr_parent = parent_side[1];
	intr_child = child_side[0];
	*ttyfd = child_side[1];
	snprintf(namebuf, namebuflen, "pipe");
	/* These threads will exist in the child too, when we fork in ptycommand.
	 * that would normally be a problem, but when we exec, we'll call
	 * pty_make_controlling_tty() and close them.
	 *
	 * It's a little nastier than that.  We're going to fork with a couple
	 * outstanding syscalls in flight.  Those do not get forked - the syscalls
	 * belong to the parent.  So even if we never execed, those uthreads would
	 * still be sitting waiting for a syscall struct that never finishes.
	 *
	 * It is important that we don't want to be an MCP - o/w fork and exec will
	 * fail.  Gotta love fork. */
	parlib_wants_to_be_mcp = FALSE;
	if (pthread_create(&child_to_parent, NULL, &intr_child_to_parent, NULL))
		handle_error("create child_to_parent");
	if (pthread_create(&parent_to_child, NULL, &intr_parent_to_child, NULL))
		handle_error("create parent_to_child");
	return 1;
}

void pty_change_window_size(int ptyfd, int row, int col, int xpixel, int ypixel)
{
	fprintf(stderr, "%s(%d, %d, %d, %d, %d): not yet\n", __func__, ptyfd, row,
	        col,xpixel, ypixel);
}

void pty_release(const char *tty_name)
{
	fprintf(stderr, "%s %s: not yet\n", __func__, tty_name);
}

void pty_make_controlling_tty(int *ttyfd, const char *tty_name)
{
	/* The rest of DB didn't know about these FDs, so we close them in the child
	 * after the fork and before the exec. */
	if (close(intr_parent))
		handle_error("closing intr_parent");
	if (close(intr_child))
		handle_error("closing intr_child");
}
