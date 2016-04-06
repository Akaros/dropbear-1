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

#include <sys/types.h>
#include <unistd.h>
#include <pwd.h>
#include <string.h>
#include <sys/time.h>
#include <sys/resource.h>

/* TODO: we can move most of these into glibc. */

struct passwd default_user = {
	.pw_name = "root",
	.pw_passwd = "x",
	.pw_uid = 0,
	.pw_gid = 0,
	.pw_gecos = "",
	.pw_dir = "/",
	.pw_shell = "/bin/sh",
};

static int getpwd_r_foobar(struct passwd *pwd, char *buf, size_t buflen,
                           struct passwd **result)
{
	memcpy(pwd, &default_user, sizeof(struct passwd));
	*result = pwd;
	return 0;
}

struct passwd *getpwnam(const char *name)
{
	return &default_user;
}

struct passwd *getpwuid(uid_t uid)
{
	return &default_user;
}

int getpwnam_r(const char *name, struct passwd *pwd,
               char *buf, size_t buflen, struct passwd **result)
{
	return getpwd_r_foobar(pwd, buf, buflen, result);
}

int getpwuid_r(uid_t uid, struct passwd *pwd, char *buf, size_t buflen,
               struct passwd **result)
{
	return getpwd_r_foobar(pwd, buf, buflen, result);
}

uid_t getuid(void)
{
	return 0;
}

uid_t geteuid(void)
{
	return 0;
}

gid_t getgid(void)
{
	return 0;
}

gid_t getegid(void)
{
	return 0;
}

int seteuid(uid_t euid)
{
	return 0;
}

int setegid(gid_t egid)
{
	return 0;
}

int setrlimit(int resource, const struct rlimit *rlim)
{
	return 0;
}

/* TODO: probably need session support or something */
pid_t setsid(void)
{
	return 0x1337;
}
