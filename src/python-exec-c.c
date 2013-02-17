/* python-exec -- a Gentoo tool to choose the correct Python script
 * variant for currently selected Python implementation.
 * (c) 2012 Michał Górny
 * Licensed under the terms of the 2-clause BSD license.
 */

#ifdef HAVE_CONFIG_H
#	include "config.h"
#endif

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* All possible EPYTHON values, provided to the configure script. */
const char* const python_impls[] = { PYTHON_IMPLS };
/* Maximum length of an EPYTHON value. */
const size_t max_epython_len = MAX_EPYTHON_LEN;

/**
 * Try to obtain EPYTHON from an environment variable.
 *
 * @bufp points to the space in the buffer where the value shall be
 * written (first byte after the hyphen). The buffer must have at least
 * max_epython_len space.
 *
 * @variable contains the environment variable name.
 *
 * Returns 1 on success, 0 otherwise.
 */
static int try_env(char* bufp, const char* variable)
{
	const char* epython = getenv(variable);

	if (epython)
	{
		if (strlen(epython) <= max_epython_len)
		{
			strcpy(bufp, epython);
			return 1;
		}
		else
			fprintf(stderr, "EPYTHON value invalid (too long).\n");
	}

	return 0;
}

/**
 * Try to read EPYTHON from a regular file.
 *
 * @bufp points to the space in the buffer where the value shall be
 * written (first byte after the hyphen). The buffer must have at least
 * max_epython_len space.
 *
 * @variable contains the file path.
 *
 * Returns 1 on success, 0 otherwise.
 */
static int try_file(char* bufp, const char* path)
{
	FILE* f = fopen(path, "r");

	if (f)
	{
		size_t rd = fread(bufp, 1, max_epython_len, f);

		if (rd > 0 && feof(f))
		{
			bufp[rd] = 0;
			if (bufp[rd-1] == '\n')
				bufp[rd-1] = 0;
		}

		fclose(f);
	}

	return !!f;
}

/**
 * Try to obtain EPYTHON from a symlink target.
 *
 * @bufp points to the space in the buffer where the value shall be
 * written (first byte after the hyphen). The buffer must have at least
 * max_epython_len space.
 *
 * @variable contains the symlink path.
 *
 * Returns 1 on success, 0 otherwise.
 */
static int try_symlink(char* bufp, const char* path)
{
	size_t rd = readlink(path, bufp, max_epython_len);

	/* [max_epython_len] could mean that the name is too long */
	if (rd > 0 && rd < max_epython_len)
	{
		bufp[rd] = 0;
		return 1;
	}

	return 0;
}

/**
 * Shift the argv array one element left. Left-most element will be
 * removed, right-most will be replaced with trailing NULL.
 *
 * If argc is used, it has to be decremented separately.
 *
 * @argv is a pointer to first argv element.
 */
static void shift_argv(char* argv[])
{
	char** i;

	for (i = argv; *i; ++i)
		i[0] = i[1];
}

/**
 * Check the buffer size and reallocate it if necessary.
 *
 * Assumes that buffers <= BUFSIZ are stack-allocated,
 * and heap-allocated above that size.
 *
 * @buf is the pointer to the buffer address holder.
 *
 * @buf_size is the pointer to the buffer size holder.
 *
 * @req_size is the requested buffer size.
 *
 * Returns 1 on success, or 0 if memory allocation failed.
 * In the latter case, buffer is left unchanged.
 */
static int expand_buffer(char** buf, size_t* buf_size, size_t req_size)
{
	if (req_size >= *buf_size)
	{
		char *new_buf;

		/* first alloc */
		if (*buf_size <= BUFSIZ)
			new_buf = malloc(req_size);
		else /* realloc */
			new_buf = realloc(*buf, req_size);

		if (!new_buf)
			return 0;
		*buf = new_buf;
		*buf_size = req_size;
	}

	return 1;
}

/**
 * Usage: python-exec <script> [<argv>...]
 *
 * python-exec tries to execute <script> with most preferred Python
 * implementation supported by it. It determines whether a particular
 * implementation is supported through appending '-${EPYTHON}'
 * to the script path.
 */
int main(int argc, char* argv[])
{
	const char* const* i;
	char buf[BUFSIZ];
	size_t buf_size = sizeof(buf);
	char* bufp = buf;
	char* bufpy;

	const char* script = argv[1];

	if (!script)
	{
		fprintf(stderr, "Usage: %s <script>\n", argv[0]);
		return EXIT_FAILURE;
	}

	{
		size_t len = strlen(script);

		/* 2 is for the hyphen and the null terminator. */
		if (!expand_buffer(&bufp, &buf_size, len + max_epython_len + 2))
		{
			fprintf(stderr, "%s: memory allocation failed (program name too long).\n",
					script);
			return EXIT_FAILURE;
		}
		memcpy(bufp, script, len);
		bufp[len] = '-';

		shift_argv(argv);

		bufpy = &bufp[len+1];

		/**
		 * The implementation check order:
		 * 1) environment variable EPYTHON (local choice),
		 * 2) eselect-python main Python interpreter,
		 * 3) eselect-python Python 2 & Python 3 choices,
		 * 4) any of the supported implementations.
		 *
		 * For 3), the order is basically irrelevant since whichever
		 * is preferred will be tried in 2) anyway.
		 *
		 * 4) uses the eclass-defined order.
		 */
		if (try_env(bufpy, "EPYTHON"))
			execvp(bufp, argv);
		if (try_file(bufpy, EPREFIX "/etc/env.d/python/config"))
			execvp(bufp, argv);
		if (try_symlink(bufpy, EPREFIX "/usr/bin/python2"))
			execvp(bufp, argv);
		if (try_symlink(bufpy, EPREFIX "/usr/bin/python3"))
			execvp(bufp, argv);

		for (i = python_impls; *i; ++i)
		{
			strcpy(&bufp[len+1], *i);
			execvp(bufp, argv);
		}
	}

	/* If no execvp() succeeded, that means we either don't have
	 * a single supported implementation here or something is seriously
	 * broken.
	 */
	if (bufp != buf)
		free(bufp);
	fprintf(stderr, "%s: no supported Python implementation variant found!\n",
			script);
	return 127;
}
