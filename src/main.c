/*
 * This file is part of Foreign Linux.
 *
 * Copyright (C) 2014, 2015 Xiangyan Sun <wishstudio@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <common/auxvec.h>
#include <syscall/exec.h>
#include <syscall/fork.h>
#include <syscall/mm.h>
#include <syscall/process.h>
#include <syscall/tls.h>
#include <syscall/vfs.h>
#include <log.h>
#include <heap.h>
#include <str.h>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#pragma comment(linker,"/entry:main")

/*
 * Startup data is divided into two parts
 * The actual used part is flipped upon execve()
 * This is to prevent data corruption when the arguments of execve() used pointers at data inside the startup area
 * The first uintptr_t data itme in each side is set to 1 when the part is currently in use
 */
static char *const startup = (char *)STARTUP_DATA_BASE;

#define ALIGN_TO(x, a) ((uintptr_t)((x) + (a) - 1) & -(a))

void main()
{
	log_init();
	fork_init();
	/* fork_init() will directly jump to restored thread context if we are a fork child */

	mm_init();
	install_syscall_handler();
	heap_init();
	vfs_init();
	tls_init();
	dbt_init();
	process_init(NULL);

	/* Parse command line */
	const char *cmdline = GetCommandLineA();
	int len = strlen(cmdline);
	if (len > BLOCK_SIZE) /* TODO: Test if there is sufficient space for argv[] array */
	{
		kprintf("Command line too long.\n");
		ExitProcess(1);
	}

	mm_mmap(STARTUP_DATA_BASE, BLOCK_SIZE, PROT_READ | PROT_WRITE, MAP_FIXED | MAP_ANONYMOUS | MAP_PRIVATE, NULL, 0);
	*(uintptr_t*) startup = 1;
	char *current_startup_base = startup + sizeof(uintptr_t);
	memcpy(current_startup_base, cmdline, len + 1);
	/* TODO: This works now, but looks too ugly */
	char *envbuf = ALIGN_TO(current_startup_base + len + 1, sizeof(void*));
	envbuf[0] = 'T';
	envbuf[1] = 'E';
	envbuf[2] = 'R';
	envbuf[3] = 'M';
	envbuf[4] = '=';
	envbuf[5] = 'x';
	envbuf[6] = 't';
	envbuf[7] = 'e';
	envbuf[8] = 'r';
	envbuf[9] = 'm';
	envbuf[10] = 0;
	int argc = 0;
	const char **argv = (const char **)(envbuf + 16);

	/* Parse command line */
	int in_quote = 0;
	const char *j = current_startup_base;
	for (char *i = current_startup_base; i <= current_startup_base + len; i++)
		if (!in_quote && (*i == ' ' || *i == '\t' || *i == '\r' || *i == '\n' || *i == 0))
		{
			*i = 0;
			if (i > j)
				argv[argc++] = j;
			j = i + 1;
		}
		else if (*i == '"')
		{
			*i = 0;
			if (in_quote)
				argv[argc++] = j;
			in_quote = !in_quote;
			j = i + 1;
		}
	argv[argc] = NULL;
	const char **envp = argv + argc + 1;
	int env_size = 1;
	envp[0] = envbuf;
	envp[1] = NULL;
	char *buffer_base = (char*)(envp + env_size + 1);

	const char *filename = NULL;
	for (int i = 1; i < argc; i++)
	{
		if (argv[i][0] == '-')
		{
		}
		else if (!filename)
			filename = argv[i];
	}
	if (filename)
		do_execve(filename, argc - 1, argv + 1, env_size, envp, buffer_base);
	kprintf("Execution failed.\n");
	ExitProcess(1);
}
