#include "libbb.h"

struct DIR {
	struct dirent dd_dir;
	HANDLE dd_handle;     /* FindFirstFile handle */
	int not_first;
	int got_dot;
	int got_dotdot;
};

static inline unsigned char get_dtype(DWORD attr, DWORD reserved0)
{
	if ((attr & FILE_ATTRIBUTE_REPARSE_POINT) &&
			(reserved0 == IO_REPARSE_TAG_SYMLINK ||
			reserved0 == IO_REPARSE_TAG_MOUNT_POINT ||
			reserved0 == IO_REPARSE_TAG_APPEXECLINK))
		return DT_LNK;
	if (attr & FILE_ATTRIBUTE_DIRECTORY)
		return DT_DIR;
	return DT_REG;
}

static inline void finddata2dirent(struct dirent *ent, WIN32_FIND_DATAW *fdata)
{
	/* copy file name from WIN32_FIND_DATAW to dirent */
	WideCharToMultiByte(CP_ACP, 0, fdata->cFileName, -1,
			ent->d_name, PATH_MAX, NULL, NULL);
	ent->d_type = get_dtype(fdata->dwFileAttributes, fdata->dwReserved0);
}

DIR *opendir(const char *name)
{
	wchar_t wpattern[32768];
	WIN32_FIND_DATAW fdata;
	HANDLE h;
	int wlen;
	DIR *dir;

	/* check that name is not NULL */
	if (!name) {
		errno = EINVAL;
		return NULL;
	}

	/* Convert to wide string.  CP_ACP is CP_UTF8 when the UTF-8 manifest
	 * is active, else the system ANSI code page. */
	wlen = MultiByteToWideChar(CP_ACP, 0, name, -1, wpattern, 32760);
	if (wlen == 0) {
		errno = EINVAL;
		return NULL;
	}
	wlen--;  /* exclude null terminator from length */

	/* append optional '\' and wildcard '*' */
	if (wlen && wpattern[wlen - 1] != L'\\' && wpattern[wlen - 1] != L'/')
		wpattern[wlen++] = L'\\';
	wpattern[wlen++] = L'*';
	wpattern[wlen] = 0;

	/* open find handle using wide API */
	h = FindFirstFileW(wpattern, &fdata);
	if (h == INVALID_HANDLE_VALUE) {
		DWORD err = GetLastError();
		errno = (err == ERROR_DIRECTORY) ? ENOTDIR : err_win_to_posix();
		return NULL;
	}

	/* initialize DIR structure and copy first dir entry */
	dir = xzalloc(sizeof(DIR));
	dir->dd_handle = h;
	/* dir->not_first = 0; */
	/* dir->got_dot = 0; */
	/* dir->got_dotdot = 0; */
	finddata2dirent(&dir->dd_dir, &fdata);
	return dir;
}

struct dirent *readdir(DIR *dir)
{
	if (!dir) {
		errno = EBADF; /* No set_errno for mingw */
		return NULL;
	}

	/* if first entry, dirent has already been set up by opendir */
	if (dir->not_first) {
		/* get next entry and convert from WIN32_FIND_DATA to dirent */
		WIN32_FIND_DATAW fdata;
		if (FindNextFileW(dir->dd_handle, &fdata)) {
			finddata2dirent(&dir->dd_dir, &fdata);
		} else if (!dir->got_dot) {
			strcpy(dir->dd_dir.d_name, ".");
			dir->dd_dir.d_type = DT_DIR;
		} else if (!dir->got_dotdot) {
			strcpy(dir->dd_dir.d_name, "..");
			dir->dd_dir.d_type = DT_DIR;
		} else {
			DWORD lasterr = GetLastError();
			/* POSIX says you shouldn't set errno when readdir can't
			   find any more files; so, if another error we leave it set. */
			if (lasterr != ERROR_NO_MORE_FILES)
				errno = err_win_to_posix();
			return NULL;
		}
	}

	/* Have we seen '.' or '..'? */
	if (dir->dd_dir.d_name[0] == '.') {
		if (dir->dd_dir.d_name[1] == '\0')
			dir->got_dot = TRUE;
		else if (dir->dd_dir.d_name[1] == '.' && dir->dd_dir.d_name[2] == '\0')
			dir->got_dotdot = TRUE;
	}

	dir->not_first = TRUE;
	return &dir->dd_dir;
}

int closedir(DIR *dir)
{
	if (!dir) {
		errno = EBADF;
		return -1;
	}

	FindClose(dir->dd_handle);
	free(dir);
	return 0;
}
