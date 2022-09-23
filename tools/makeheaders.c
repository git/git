/*
** This program is free software; you can redistribute it and/or
** modify it under the terms of the Simplified BSD License (also
** known as the "2-Clause License" or "FreeBSD License".)
**
** Copyright 1993 D. Richard Hipp. All rights reserved.
**
** Redistribution and use in source and binary forms, with or
** without modification, are permitted provided that the following
** conditions are met:
**
**   1. Redistributions of source code must retain the above copyright
**      notice, this list of conditions and the following disclaimer.
**
**   2. Redistributions in binary form must reproduce the above copyright
**      notice, this list of conditions and the following disclaimer in
**      the documentation and/or other materials provided with the
**      distribution.
**
** This software is provided "as is" and any express or implied warranties,
** including, but not limited to, the implied warranties of merchantability
** and fitness for a particular purpose are disclaimed.  In no event shall
** the author or contributors be liable for any direct, indirect, incidental,
** special, exemplary, or consequential damages (including, but not limited
** to, procurement of substitute goods or services; loss of use, data or
** profits; or business interruption) however caused and on any theory of
** liability, whether in contract, strict liability, or tort (including
** negligence or otherwise) arising in any way out of the use of this
** software, even if advised of the possibility of such damage.
**
** This program is distributed in the hope that it will be useful,
** but without any warranty; without even the implied warranty of
** merchantability or fitness for a particular purpose.
*/
#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <memory.h>
#include <sys/stat.h>
#include <assert.h>
#include <string.h>

#if defined(__MINGW32__) || defined(__DMC__) || defined(_MSC_VER) || \
	defined(__POCC__)
#ifndef WIN32
#define WIN32
#endif
#else
#include <unistd.h>
#endif

/*
** Macros for debugging.
*/
#ifdef DEBUG
static int debugMask = 0;
#define debug0(F, M)                \
	if ((F)&debugMask) {        \
		fprintf(stderr, M); \
	}
#define debug1(F, M, A)                \
	if ((F)&debugMask) {           \
		fprintf(stderr, M, A); \
	}
#define debug2(F, M, A, B)                \
	if ((F)&debugMask) {              \
		fprintf(stderr, M, A, B); \
	}
#define debug3(F, M, A, B, C)                \
	if ((F)&debugMask) {                 \
		fprintf(stderr, M, A, B, C); \
	}
#define PARSER 0x00000001
#define DECL_DUMP 0x00000002
#define TOKENIZER 0x00000004
#else
#define debug0(Flags, Format)
#define debug1(Flags, Format, A)
#define debug2(Flags, Format, A, B)
#define debug3(Flags, Format, A, B, C)
#endif

/*
** The following macros are purely for the purpose of testing this
** program on itself.  They don't really contribute to the code.
*/
#define INTERFACE 1
#define EXPORT_INTERFACE 1
#define EXPORT

/*
** Each token in a source file is represented by an instance of
** the following structure.  Tokens are collected onto a list.
*/
typedef struct Token Token;
struct Token {
	const char *zText; /* The text of the token */
	int nText; /* Number of characters in the token's text */
	int eType; /* The type of this token */
	int nLine; /* The line number on which the token starts */
	Token *pComment; /* Most recent block comment before this token */
	Token *pNext; /* Next token on the list */
	Token *pPrev; /* Previous token on the list */
};

/*
** During tokenization, information about the state of the input
** stream is held in an instance of the following structure
*/
typedef struct InStream InStream;
struct InStream {
	const char *z; /* Complete text of the input */
	int i; /* Next character to read from the input */
	int nLine; /* The line number for character z[i] */
};

/*
** Each declaration in the C or C++ source files is parsed out and stored as
** an instance of the following structure.
**
** A "forward declaration" is a declaration that an object exists that
** doesn't tell about the objects structure.  A typical forward declaration
** is:
**
**          struct Xyzzy;
**
** Not every object has a forward declaration.  If it does, thought, the
** forward declaration will be contained in the zFwd field for C and
** the zFwdCpp for C++.  The zDecl field contains the complete
** declaration text.
*/
typedef struct Decl Decl;
struct Decl {
	char *zName; /* Name of the object being declared.  The appearance
		     ** of this name is a source file triggers the declaration
		     ** to be added to the header for that file. */
	const char *zFile; /* File from which extracted.  */
	char *zIf; /* Surround the declaration with this #if */
	char *zFwd; /* A forward declaration.  NULL if there is none. */
	char *zFwdCpp; /* Use this forward declaration for C++. */
	char *zDecl; /* A full declaration of this object */
	char *zExtra; /* Extra declaration text inserted into class objects */
	int extraType; /* Last public:, protected: or private: in zExtraDecl */
	struct Include *pInclude; /* #includes that come before this declaration
				   */
	int flags; /* See the "Properties" below */
	Token *pComment; /* A block comment associated with this declaration */
	Token tokenCode; /* Implementation of functions and procedures */
	Decl *pSameName; /* Next declaration with the same "zName" */
	Decl *pSameHash; /* Next declaration with same hash but different zName
			  */
	Decl *pNext; /* Next declaration with a different name */
};

/*
** Properties associated with declarations.
**
** DP_Forward and DP_Declared are used during the generation of a single
** header file in order to prevent duplicate declarations and definitions.
** DP_Forward is set after the object has been given a forward declaration
** and DP_Declared is set after the object gets a full declarations.
** (Example:  A forward declaration is "typedef struct Abc Abc;" and the
** full declaration is "struct Abc { int a; float b; };".)
**
** The DP_Export and DP_Local flags are more permanent.  They mark objects
** that have EXPORT scope and LOCAL scope respectively.  If both of these
** marks are missing, then the object has library scope.  The meanings of
** the scopes are as follows:
**
**    LOCAL scope         The object is only usable within the file in
**                        which it is declared.
**
**    library scope       The object is visible and usable within other
**                        files in the same project.  By if the project is
**                        a library, then the object is not visible to users
**                        of the library.  (i.e. the object does not appear
**                        in the output when using the -H option.)
**
**    EXPORT scope        The object is visible and usable everywhere.
**
** The DP_Flag is a temporary use flag that is used during processing to
** prevent an infinite loop.  It's use is localized.
**
** The DP_Cplusplus, DP_ExternCReqd and DP_ExternReqd flags are permanent
** and are used to specify what type of declaration the object requires.
*/
#define DP_Forward 0x001 /* Has a forward declaration in this file */
#define DP_Declared 0x002 /* Has a full declaration in this file */
#define DP_Export 0x004 /* Export this declaration */
#define DP_Local 0x008 /* Declare in its home file only */
#define DP_Flag                                      \
	0x010 /* Use to mark a subset of a Decl list \
	      ** for special processing */
#define DP_Cplusplus                                    \
	0x020 /* Has C++ linkage and cannot appear in a \
	      ** C header file */
#define DP_ExternCReqd                                 \
	0x040 /* Prepend 'extern "C"' in a C++ header. \
	      ** Prepend nothing in a C header */
#define DP_ExternReqd                                          \
	0x080 /* Prepend 'extern "C"' in a C++ header if       \
	      ** DP_Cplusplus is not also set. If DP_Cplusplus \
	      ** is set or this is a C header then             \
	      ** prepend 'extern' */

/*
** Convenience macros for dealing with declaration properties
*/
#define DeclHasProperty(D, P) (((D)->flags & (P)) == (P))
#define DeclHasAnyProperty(D, P) (((D)->flags & (P)) != 0)
#define DeclSetProperty(D, P) (D)->flags |= (P)
#define DeclClearProperty(D, P) (D)->flags &= ~(P)

/*
** These are state properties of the parser.  Each of the values is
** distinct from the DP_ values above so that both can be used in
** the same "flags" field.
**
** Be careful not to confuse PS_Export with DP_Export or
** PS_Local with DP_Local.  Their names are similar, but the meanings
** of these flags are very different.
*/
#define PS_Extern 0x000800 /* "extern" has been seen */
#define PS_Export                                     \
	0x001000 /* If between "#if EXPORT_INTERFACE" \
		 ** and "#endif" */
#define PS_Export2 0x002000 /* If "EXPORT" seen */
#define PS_Typedef 0x004000 /* If "typedef" has been seen */
#define PS_Static 0x008000 /* If "static" has been seen */
#define PS_Interface 0x010000 /* If within #if INTERFACE..#endif */
#define PS_Method 0x020000 /* If "::" token has been seen */
#define PS_Local 0x040000 /* If within #if LOCAL_INTERFACE..#endif */
#define PS_Local2 0x080000 /* If "LOCAL" seen. */
#define PS_Public 0x100000 /* If "PUBLIC" seen. */
#define PS_Protected 0x200000 /* If "PROTECTED" seen. */
#define PS_Private 0x400000 /* If "PRIVATE" seen. */
#define PS_PPP 0x700000 /* If any of PUBLIC, PRIVATE, PROTECTED */

/*
** The following set of flags are ORed into the "flags" field of
** a Decl in order to identify what type of object is being
** declared.
*/
#define TY_Class 0x00100000
#define TY_Subroutine 0x00200000
#define TY_Macro 0x00400000
#define TY_Typedef 0x00800000
#define TY_Variable 0x01000000
#define TY_Structure 0x02000000
#define TY_Union 0x04000000
#define TY_Enumeration 0x08000000
#define TY_Defunct 0x10000000 /* Used to erase a declaration */

/*
** Each nested #if (or #ifdef or #ifndef) is stored in a stack of
** instances of the following structure.
*/
typedef struct Ifmacro Ifmacro;
struct Ifmacro {
	int nLine; /* Line number where this macro occurs */
	char *zCondition; /* Text of the condition for this macro */
	Ifmacro *pNext; /* Next down in the stack */
	int flags; /* Can hold PS_Export, PS_Interface or PS_Local flags */
};

/*
** When parsing a file, we need to keep track of what other files have
** be #include-ed.  For each #include found, we create an instance of
** the following structure.
*/
typedef struct Include Include;
struct Include {
	char *zFile; /* The name of file include.  Includes "" or <> */
	char *zIf; /* If not NULL, #include should be enclosed in #if */
	char *zLabel; /* A unique label used to test if this #include has
		       * appeared already in a file or not */
	Include *pNext; /* Previous include file, or NULL if this is the first
			 */
};

/*
** Identifiers found in a source file that might be used later to provoke
** the copying of a declaration into the corresponding header file are
** stored in a hash table as instances of the following structure.
*/
typedef struct Ident Ident;
struct Ident {
	char *zName; /* The text of this identifier */
	Ident *pCollide; /* Next identifier with the same hash */
	Ident *pNext; /* Next identifier in a list of them all */
};

/*
** A complete table of identifiers is stored in an instance of
** the next structure.
*/
#define IDENT_HASH_SIZE 2237
typedef struct IdentTable IdentTable;
struct IdentTable {
	Ident *pList; /* List of all identifiers in this table */
	Ident *apTable[IDENT_HASH_SIZE]; /* The hash table */
};

/*
** The following structure holds all information for a single
** source file named on the command line of this program.
*/
typedef struct InFile InFile;
struct InFile {
	char *zSrc; /* Name of input file */
	char *zHdr; /* Name of the generated .h file for this input.
		    ** Will be NULL if input is to be scanned only */
	int flags; /* One or more DP_, PS_ and/or TY_ flags */
	InFile *pNext; /* Next input file in the list of them all */
	IdentTable idTable; /* All identifiers in this input file */
};

/*
** An unbounded string is able to grow without limit.  We use these
** to construct large in-memory strings from lots of smaller components.
*/
typedef struct String String;
struct String {
	int nAlloc; /* Number of bytes allocated */
	int nUsed; /* Number of bytes used (not counting nul terminator) */
	char *zText; /* Text of the string */
};

/*
** The following structure contains a lot of state information used
** while generating a .h file.  We put the information in this structure
** and pass around a pointer to this structure, rather than pass around
** all of the information separately.  This helps reduce the number of
** arguments to generator functions.
*/
typedef struct GenState GenState;
struct GenState {
	String *pStr; /* Write output to this string */
	IdentTable *pTable; /* A table holding the zLabel of every #include that
			     * has already been generated.  Used to avoid
			     * generating duplicate #includes. */
	const char *zIf; /* If not NULL, then we are within a #if with
			  * this argument. */
	int nErr; /* Number of errors */
	const char *zFilename; /* Name of the source file being scanned */
	int flags; /* Various flags (DP_ and PS_ flags above) */
};

/*
** The following text line appears at the top of every file generated
** by this program.  By recognizing this line, the program can be sure
** never to read a file that it generated itself.
**
** The "#undef INTERFACE" part is a hack to work around a name collision
** in MSVC 2008.
*/
const char zTopLine[] =
	"/* \aThis file was automatically generated.  Do not edit! */\n"
	"#undef INTERFACE\n";
#define nTopLine (sizeof(zTopLine) - 1)

/*
** The name of the file currently being parsed.
*/
static const char *zFilename;

/*
** The stack of #if macros for the file currently being parsed.
*/
static Ifmacro *ifStack = 0;

/*
** A list of all files that have been #included so far in a file being
** parsed.
*/
static Include *includeList = 0;

/*
** The last block comment seen.
*/
static Token *blockComment = 0;

/*
** The following flag is set if the -doc flag appears on the
** command line.
*/
static int doc_flag = 0;

/*
** If the following flag is set, then makeheaders will attempt to
** generate prototypes for static functions and procedures.
*/
static int proto_static = 0;

/*
** A list of all declarations.  The list is held together using the
** pNext field of the Decl structure.
*/
static Decl *pDeclFirst; /* First on the list */
static Decl *pDeclLast; /* Last on the list */

/*
** A hash table of all declarations
*/
#define DECL_HASH_SIZE 3371
static Decl *apTable[DECL_HASH_SIZE];

/*
** The TEST macro must be defined to something.  Make sure this is the
** case.
*/
#ifndef TEST
#define TEST 0
#endif

#ifdef NOT_USED
/*
** We do our own assertion macro so that we can have more control
** over debugging.
*/
#define Assert(X)                     \
	if (!(X)) {                   \
		CantHappen(__LINE__); \
	}
#define CANT_HAPPEN CantHappen(__LINE__)
static void CantHappen(int iLine)
{
	fprintf(stderr, "Assertion failed on line %d\n", iLine);
	*(char *)1 = 0; /* Force a core-dump */
}
#endif

/*
** Memory allocation functions that are guaranteed never to return NULL.
*/
static void *SafeMalloc(int nByte)
{
	void *p = malloc(nByte);
	if (p == 0) {
		fprintf(stderr, "Out of memory.  Can't allocate %d bytes.\n",
			nByte);
		exit(1);
	}
	return p;
}
static void SafeFree(void *pOld)
{
	if (pOld) {
		free(pOld);
	}
}
static void *SafeRealloc(void *pOld, int nByte)
{
	void *p;
	if (pOld == 0) {
		p = SafeMalloc(nByte);
	} else {
		p = realloc(pOld, nByte);
		if (p == 0) {
			fprintf(stderr,
				"Out of memory.  Can't enlarge an allocation to %d bytes\n",
				nByte);
			exit(1);
		}
	}
	return p;
}
static char *StrDup(const char *zSrc, int nByte)
{
	char *zDest;
	if (nByte <= 0) {
		nByte = strlen(zSrc);
	}
	zDest = SafeMalloc(nByte + 1);
	strncpy(zDest, zSrc, nByte);
	zDest[nByte] = 0;
	return zDest;
}

/*
** Return TRUE if the character X can be part of an identifier
*/
#define ISALNUM(X) ((X) == '_' || isalnum(X))

/*
** Routines for dealing with unbounded strings.
*/
static void StringInit(String *pStr)
{
	pStr->nAlloc = 0;
	pStr->nUsed = 0;
	pStr->zText = 0;
}
static void StringReset(String *pStr)
{
	SafeFree(pStr->zText);
	StringInit(pStr);
}
static void StringAppend(String *pStr, const char *zText, int nByte)
{
	if (nByte <= 0) {
		nByte = strlen(zText);
	}
	if (pStr->nUsed + nByte >= pStr->nAlloc) {
		if (pStr->nAlloc == 0) {
			pStr->nAlloc = nByte + 100;
			pStr->zText = SafeMalloc(pStr->nAlloc);
		} else {
			pStr->nAlloc = pStr->nAlloc * 2 + nByte;
			pStr->zText = SafeRealloc(pStr->zText, pStr->nAlloc);
		}
	}
	strncpy(&pStr->zText[pStr->nUsed], zText, nByte);
	pStr->nUsed += nByte;
	pStr->zText[pStr->nUsed] = 0;
}
#define StringGet(S) ((S)->zText ? (S)->zText : "")

/*
** Compute a hash on a string.  The number returned is a non-negative
** value between 0 and 2**31 - 1
*/
static int Hash(const char *z, int n)
{
	int h = 0;
	if (n <= 0) {
		n = strlen(z);
	}
	while (n--) {
		h = h ^ (h << 5) ^ *z++;
	}
	return h & 0x7fffffff;
}

/*
** Given an identifier name, try to find a declaration for that
** identifier in the hash table.  If found, return a pointer to
** the Decl structure.  If not found, return 0.
*/
static Decl *FindDecl(const char *zName, int len)
{
	int h;
	Decl *p;

	if (len <= 0) {
		len = strlen(zName);
	}
	h = Hash(zName, len) % DECL_HASH_SIZE;
	p = apTable[h];
	while (p &&
	       (strncmp(p->zName, zName, len) != 0 || p->zName[len] != 0)) {
		p = p->pSameHash;
	}
	return p;
}

/*
** Install the given declaration both in the hash table and on
** the list of all declarations.
*/
static void InstallDecl(Decl *pDecl)
{
	int h;
	Decl *pOther;

	h = Hash(pDecl->zName, 0) % DECL_HASH_SIZE;
	pOther = apTable[h];
	while (pOther && strcmp(pDecl->zName, pOther->zName) != 0) {
		pOther = pOther->pSameHash;
	}
	if (pOther) {
		pDecl->pSameName = pOther->pSameName;
		pOther->pSameName = pDecl;
	} else {
		pDecl->pSameName = 0;
		pDecl->pSameHash = apTable[h];
		apTable[h] = pDecl;
	}
	pDecl->pNext = 0;
	if (pDeclFirst == 0) {
		pDeclFirst = pDeclLast = pDecl;
	} else {
		pDeclLast->pNext = pDecl;
		pDeclLast = pDecl;
	}
}

/*
** Look at the current ifStack.  If anything declared at the current
** position must be surrounded with
**
**      #if   STUFF
**      #endif
**
** Then this routine computes STUFF and returns a pointer to it.  Memory
** to hold the value returned is obtained from malloc().
*/
static char *GetIfString(void)
{
	Ifmacro *pIf;
	char *zResult = 0;
	int hasIf = 0;
	String str;

	for (pIf = ifStack; pIf; pIf = pIf->pNext) {
		if (pIf->zCondition == 0 || *pIf->zCondition == 0)
			continue;
		if (!hasIf) {
			hasIf = 1;
			StringInit(&str);
		} else {
			StringAppend(&str, " && ", 4);
		}
		StringAppend(&str, pIf->zCondition, 0);
	}
	if (hasIf) {
		zResult = StrDup(StringGet(&str), 0);
		StringReset(&str);
	} else {
		zResult = 0;
	}
	return zResult;
}

/*
** Create a new declaration and put it in the hash table.  Also
** return a pointer to it so that we can fill in the zFwd and zDecl
** fields, and so forth.
*/
static Decl *CreateDecl(const char *zName, /* Name of the object being declared.
					    */
			int nName /* Length of the name */
)
{
	Decl *pDecl;

	pDecl = SafeMalloc(sizeof(Decl) + nName + 1);
	memset(pDecl, 0, sizeof(Decl));
	pDecl->zName = (char *)&pDecl[1];
	sprintf(pDecl->zName, "%.*s", nName, zName);
	pDecl->zFile = zFilename;
	pDecl->pInclude = includeList;
	pDecl->zIf = GetIfString();
	InstallDecl(pDecl);
	return pDecl;
}

/*
** Insert a new identifier into an table of identifiers.  Return TRUE if
** a new identifier was inserted and return FALSE if the identifier was
** already in the table.
*/
static int IdentTableInsert(IdentTable *pTable, /* The table into which we will
						   insert */
			    const char *zId, /* Name of the identifiers */
			    int nId /* Length of the identifier name */
)
{
	int h;
	Ident *pId;

	if (nId <= 0) {
		nId = strlen(zId);
	}
	h = Hash(zId, nId) % IDENT_HASH_SIZE;
	for (pId = pTable->apTable[h]; pId; pId = pId->pCollide) {
		if (strncmp(zId, pId->zName, nId) == 0 &&
		    pId->zName[nId] == 0) {
			/* printf("Already in table: %.*s\n",nId,zId); */
			return 0;
		}
	}
	pId = SafeMalloc(sizeof(Ident) + nId + 1);
	pId->zName = (char *)&pId[1];
	sprintf(pId->zName, "%.*s", nId, zId);
	pId->pNext = pTable->pList;
	pTable->pList = pId;
	pId->pCollide = pTable->apTable[h];
	pTable->apTable[h] = pId;
	/* printf("Add to table: %.*s\n",nId,zId); */
	return 1;
}

/*
** Check to see if the given value is in the given IdentTable.  Return
** true if it is and false if it is not.
*/
static int IdentTableTest(IdentTable *pTable, /* The table in which to search */
			  const char *zId, /* Name of the identifiers */
			  int nId /* Length of the identifier name */
)
{
	int h;
	Ident *pId;

	if (nId <= 0) {
		nId = strlen(zId);
	}
	h = Hash(zId, nId) % IDENT_HASH_SIZE;
	for (pId = pTable->apTable[h]; pId; pId = pId->pCollide) {
		if (strncmp(zId, pId->zName, nId) == 0 &&
		    pId->zName[nId] == 0) {
			return 1;
		}
	}
	return 0;
}

/*
** Remove every identifier from the given table.   Reset the table to
** its initial state.
*/
static void IdentTableReset(IdentTable *pTable)
{
	Ident *pId, *pNext;

	for (pId = pTable->pList; pId; pId = pNext) {
		pNext = pId->pNext;
		SafeFree(pId);
	}
	memset(pTable, 0, sizeof(IdentTable));
}

#ifdef DEBUG
/*
** Print the name of every identifier in the given table, one per line
*/
static void IdentTablePrint(IdentTable *pTable, FILE *pOut)
{
	Ident *pId;

	for (pId = pTable->pList; pId; pId = pId->pNext) {
		fprintf(pOut, "%s\n", pId->zName);
	}
}
#endif

/*
** Read an entire file into memory.  Return a pointer to the memory.
**
** The memory is obtained from SafeMalloc and must be freed by the
** calling function.
**
** If the read fails for any reason, 0 is returned.
*/
static char *ReadFile(const char *zFilename)
{
	struct stat sStat;
	FILE *pIn;
	char *zBuf;
	int n;

	if (stat(zFilename, &sStat) != 0
#ifndef WIN32
	    || !S_ISREG(sStat.st_mode)
#endif
	) {
		return 0;
	}
	pIn = fopen(zFilename, "r");
	if (pIn == 0) {
		return 0;
	}
	zBuf = SafeMalloc(sStat.st_size + 1);
	n = fread(zBuf, 1, sStat.st_size, pIn);
	zBuf[n] = 0;
	fclose(pIn);
	return zBuf;
}

/*
** Write the contents of a string into a file.  Return the number of
** errors
*/
static int WriteFile(const char *zFilename, const char *zOutput)
{
	FILE *pOut;
	pOut = fopen(zFilename, "w");
	if (pOut == 0) {
		return 1;
	}
	fwrite(zOutput, 1, strlen(zOutput), pOut);
	fclose(pOut);
	return 0;
}

/*
** Major token types
*/
#define TT_Space 1 /* Contiguous white space */
#define TT_Id 2 /* An identifier */
#define TT_Preprocessor 3 /* Any C preprocessor directive */
#define TT_Comment 4 /* Either C or C++ style comment */
#define TT_Number 5 /* Any numeric constant */
#define TT_String 6 /* String or character constants. ".." or '.' */
#define TT_Braces 7 /* All text between { and a matching } */
#define TT_EOF 8 /* End of file */
#define TT_Error 9 /* An error condition */
#define TT_BlockComment                                 \
	10 /* A C-Style comment at the left margin that \
	    * spans multiple lines */
#define TT_Other 0 /* None of the above */

/*
** Get a single low-level token from the input file.  Update the
** file pointer so that it points to the first character beyond the
** token.
**
** A "low-level token" is any token except TT_Braces.  A TT_Braces token
** consists of many smaller tokens and is assembled by a routine that
** calls this one.
**
** The function returns the number of errors.  An error is an
** unterminated string or character literal or an unterminated
** comment.
**
** Profiling shows that this routine consumes about half the
** CPU time on a typical run of makeheaders.
*/
static int GetToken(InStream *pIn, Token *pToken)
{
	int i;
	const char *z;
	int cStart;
	int c;
	int startLine; /* Line on which a structure begins */
	int nlisc = 0; /* True if there is a new-line in a ".." or '..' */
	int nErr = 0; /* Number of errors seen */

	z = pIn->z;
	i = pIn->i;
	pToken->nLine = pIn->nLine;
	pToken->zText = &z[i];
	switch (z[i]) {
	case 0:
		pToken->eType = TT_EOF;
		pToken->nText = 0;
		break;

	case '#':
		if (i == 0 || z[i - 1] == '\n' ||
		    (i > 1 && z[i - 1] == '\r' && z[i - 2] == '\n')) {
			/* We found a preprocessor statement */
			pToken->eType = TT_Preprocessor;
			i++;
			while (z[i] != 0 && z[i] != '\n') {
				if (z[i] == '\\') {
					i++;
					if (z[i] == '\n')
						pIn->nLine++;
				}
				i++;
			}
			pToken->nText = i - pIn->i;
		} else {
			/* Just an operator */
			pToken->eType = TT_Other;
			pToken->nText = 1;
		}
		break;

	case ' ':
	case '\t':
	case '\r':
	case '\f':
	case '\n':
		while (isspace(z[i])) {
			if (z[i] == '\n')
				pIn->nLine++;
			i++;
		}
		pToken->eType = TT_Space;
		pToken->nText = i - pIn->i;
		break;

	case '\\':
		pToken->nText = 2;
		pToken->eType = TT_Other;
		if (z[i + 1] == '\n') {
			pIn->nLine++;
			pToken->eType = TT_Space;
		} else if (z[i + 1] == 0) {
			pToken->nText = 1;
		}
		break;

	case '\'':
	case '\"':
		cStart = z[i];
		startLine = pIn->nLine;
		do {
			i++;
			c = z[i];
			if (c == '\n') {
				if (!nlisc) {
					fprintf(stderr,
						"%s:%d: (warning) Newline in string or character literal.\n",
						zFilename, pIn->nLine);
					nlisc = 1;
				}
				pIn->nLine++;
			}
			if (c == '\\') {
				i++;
				c = z[i];
				if (c == '\n') {
					pIn->nLine++;
				}
			} else if (c == cStart) {
				i++;
				c = 0;
			} else if (c == 0) {
				fprintf(stderr,
					"%s:%d: Unterminated string or character literal.\n",
					zFilename, startLine);
				nErr++;
			}
		} while (c);
		pToken->eType = TT_String;
		pToken->nText = i - pIn->i;
		break;

	case '/':
		if (z[i + 1] == '/') {
			/* C++ style comment */
			while (z[i] && z[i] != '\n') {
				i++;
			}
			pToken->eType = TT_Comment;
			pToken->nText = i - pIn->i;
		} else if (z[i + 1] == '*') {
			/* C style comment */
			int isBlockComment = i == 0 || z[i - 1] == '\n';
			i += 2;
			startLine = pIn->nLine;
			while (z[i] && (z[i] != '*' || z[i + 1] != '/')) {
				if (z[i] == '\n') {
					pIn->nLine++;
					if (isBlockComment) {
						if (z[i + 1] == '*' ||
						    z[i + 2] == '*') {
							isBlockComment = 2;
						} else {
							isBlockComment = 0;
						}
					}
				}
				i++;
			}
			if (z[i]) {
				i += 2;
			} else {
				isBlockComment = 0;
				fprintf(stderr, "%s:%d: Unterminated comment\n",
					zFilename, startLine);
				nErr++;
			}
			pToken->eType = isBlockComment == 2 ? TT_BlockComment :
							      TT_Comment;
			pToken->nText = i - pIn->i;
		} else {
			/* A divide operator */
			pToken->eType = TT_Other;
			pToken->nText = 1 + (z[i + 1] == '+');
		}
		break;

	case '0':
		if (z[i + 1] == 'x' || z[i + 1] == 'X') {
			/* A hex constant */
			i += 2;
			while (isxdigit(z[i])) {
				i++;
			}
		} else {
			/* An octal constant */
			while (isdigit(z[i])) {
				i++;
			}
		}
		pToken->eType = TT_Number;
		pToken->nText = i - pIn->i;
		break;

	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		while (isdigit(z[i])) {
			i++;
		}
		if ((c = z[i]) == '.') {
			i++;
			while (isdigit(z[i])) {
				i++;
			}
			c = z[i];
			if (c == 'e' || c == 'E') {
				i++;
				if (((c = z[i]) == '+' || c == '-') &&
				    isdigit(z[i + 1])) {
					i++;
				}
				while (isdigit(z[i])) {
					i++;
				}
				c = z[i];
			}
			if (c == 'f' || c == 'F' || c == 'l' || c == 'L') {
				i++;
			}
		} else if (c == 'e' || c == 'E') {
			i++;
			if (((c = z[i]) == '+' || c == '-') &&
			    isdigit(z[i + 1])) {
				i++;
			}
			while (isdigit(z[i])) {
				i++;
			}
		} else if (c == 'L' || c == 'l') {
			i++;
			c = z[i];
			if (c == 'u' || c == 'U') {
				i++;
			}
		} else if (c == 'u' || c == 'U') {
			i++;
			c = z[i];
			if (c == 'l' || c == 'L') {
				i++;
			}
		}
		pToken->eType = TT_Number;
		pToken->nText = i - pIn->i;
		break;

	case 'a':
	case 'b':
	case 'c':
	case 'd':
	case 'e':
	case 'f':
	case 'g':
	case 'h':
	case 'i':
	case 'j':
	case 'k':
	case 'l':
	case 'm':
	case 'n':
	case 'o':
	case 'p':
	case 'q':
	case 'r':
	case 's':
	case 't':
	case 'u':
	case 'v':
	case 'w':
	case 'x':
	case 'y':
	case 'z':
	case 'A':
	case 'B':
	case 'C':
	case 'D':
	case 'E':
	case 'F':
	case 'G':
	case 'H':
	case 'I':
	case 'J':
	case 'K':
	case 'L':
	case 'M':
	case 'N':
	case 'O':
	case 'P':
	case 'Q':
	case 'R':
	case 'S':
	case 'T':
	case 'U':
	case 'V':
	case 'W':
	case 'X':
	case 'Y':
	case 'Z':
	case '_':
		while (isalnum(z[i]) || z[i] == '_') {
			i++;
		};
		pToken->eType = TT_Id;
		pToken->nText = i - pIn->i;
		break;

	case ':':
		pToken->eType = TT_Other;
		pToken->nText = 1 + (z[i + 1] == ':');
		break;

	case '=':
	case '<':
	case '>':
	case '+':
	case '-':
	case '*':
	case '%':
	case '^':
	case '&':
	case '|':
		pToken->eType = TT_Other;
		pToken->nText = 1 + (z[i + 1] == '=');
		break;

	default:
		pToken->eType = TT_Other;
		pToken->nText = 1;
		break;
	}
	pIn->i += pToken->nText;
	return nErr;
}

/*
** This routine recovers the next token from the input file which is
** not a space or a comment or any text between an "#if 0" and "#endif".
**
** This routine returns the number of errors encountered.  An error
** is an unterminated token or unmatched "#if 0".
**
** Profiling shows that this routine uses about a quarter of the
** CPU time in a typical run.
*/
static int GetNonspaceToken(InStream *pIn, Token *pToken)
{
	int nIf = 0;
	int inZero = 0;
	const char *z;
	int value;
	int startLine;
	int nErr = 0;

	startLine = pIn->nLine;
	while (1) {
		nErr += GetToken(pIn, pToken);
		/* printf("%04d: Type=%d nIf=%d [%.*s]\n",
		   pToken->nLine,pToken->eType,nIf,pToken->nText,
		   pToken->eType!=TT_Space ? pToken->zText : "<space>"); */
		pToken->pComment = blockComment;
		switch (pToken->eType) {
		case TT_Comment: /*0123456789 12345678 */
			if (strncmp(pToken->zText, "/*MAKEHEADERS-STOP", 18) ==
			    0)
				return nErr;
			break;

		case TT_Space:
			break;

		case TT_BlockComment:
			if (doc_flag) {
				blockComment = SafeMalloc(sizeof(Token));
				*blockComment = *pToken;
			}
			break;

		case TT_EOF:
			if (nIf) {
				fprintf(stderr, "%s:%d: Unterminated \"#if\"\n",
					zFilename, startLine);
				nErr++;
			}
			return nErr;

		case TT_Preprocessor:
			z = &pToken->zText[1];
			while (*z == ' ' || *z == '\t')
				z++;
			if (sscanf(z, "if %d", &value) == 1 && value == 0) {
				nIf++;
				inZero = 1;
			} else if (inZero) {
				if (strncmp(z, "if", 2) == 0) {
					nIf++;
				} else if (strncmp(z, "endif", 5) == 0) {
					nIf--;
					if (nIf == 0)
						inZero = 0;
				}
			} else {
				return nErr;
			}
			break;

		default:
			if (!inZero) {
				return nErr;
			}
			break;
		}
	}
	/* NOT REACHED */
}

/*
** This routine looks for identifiers (strings of contiguous alphanumeric
** characters) within a preprocessor directive and adds every such string
** found to the given identifier table
*/
static void FindIdentifiersInMacro(Token *pToken, IdentTable *pTable)
{
	Token sToken;
	InStream sIn;
	int go = 1;

	sIn.z = pToken->zText;
	sIn.i = 1;
	sIn.nLine = 1;
	while (go && sIn.i < pToken->nText) {
		GetToken(&sIn, &sToken);
		switch (sToken.eType) {
		case TT_Id:
			IdentTableInsert(pTable, sToken.zText, sToken.nText);
			break;

		case TT_EOF:
			go = 0;
			break;

		default:
			break;
		}
	}
}

/*
** This routine gets the next token.  Everything contained within
** {...} is collapsed into a single TT_Braces token.  Whitespace is
** omitted.
**
** If pTable is not NULL, then insert every identifier seen into the
** IdentTable.  This includes any identifiers seen inside of {...}.
**
** The number of errors encountered is returned.  An error is an
** unterminated token.
*/
static int GetBigToken(InStream *pIn, Token *pToken, IdentTable *pTable)
{
	const char *zStart;
	int iStart;
	int nBrace;
	int c;
	int nLine;
	int nErr;

	nErr = GetNonspaceToken(pIn, pToken);
	switch (pToken->eType) {
	case TT_Id:
		if (pTable != 0) {
			IdentTableInsert(pTable, pToken->zText, pToken->nText);
		}
		return nErr;

	case TT_Preprocessor:
		if (pTable != 0) {
			FindIdentifiersInMacro(pToken, pTable);
		}
		return nErr;

	case TT_Other:
		if (pToken->zText[0] == '{')
			break;
		return nErr;

	default:
		return nErr;
	}

	iStart = pIn->i;
	zStart = pToken->zText;
	nLine = pToken->nLine;
	nBrace = 1;
	while (nBrace) {
		nErr += GetNonspaceToken(pIn, pToken);
		/* printf("%04d: nBrace=%d [%.*s]\n",pToken->nLine,nBrace,
		   pToken->nText,pToken->zText); */
		switch (pToken->eType) {
		case TT_EOF:
			fprintf(stderr, "%s:%d: Unterminated \"{\"\n",
				zFilename, nLine);
			nErr++;
			pToken->eType = TT_Error;
			return nErr;

		case TT_Id:
			if (pTable) {
				IdentTableInsert(pTable, pToken->zText,
						 pToken->nText);
			}
			break;

		case TT_Preprocessor:
			if (pTable != 0) {
				FindIdentifiersInMacro(pToken, pTable);
			}
			break;

		case TT_Other:
			if ((c = pToken->zText[0]) == '{') {
				nBrace++;
			} else if (c == '}') {
				nBrace--;
			}
			break;

		default:
			break;
		}
	}
	pToken->eType = TT_Braces;
	pToken->nText = 1 + pIn->i - iStart;
	pToken->zText = zStart;
	pToken->nLine = nLine;
	return nErr;
}

/*
** This routine frees up a list of Tokens.  The pComment tokens are
** not cleared by this.  So we leak a little memory when using the -doc
** option.  So what.
*/
static void FreeTokenList(Token *pList)
{
	Token *pNext;
	while (pList) {
		pNext = pList->pNext;
		SafeFree(pList);
		pList = pNext;
	}
}

/*
** Tokenize an entire file.  Return a pointer to the list of tokens.
**
** Space for each token is obtained from a separate malloc() call.  The
** calling function is responsible for freeing this space.
**
** If pTable is not NULL, then fill the table with all identifiers seen in
** the input file.
*/
static Token *TokenizeFile(const char *zFile, IdentTable *pTable)
{
	InStream sIn;
	Token *pFirst = 0, *pLast = 0, *pNew;
	int nErr = 0;

	sIn.z = zFile;
	sIn.i = 0;
	sIn.nLine = 1;
	blockComment = 0;

	while (sIn.z[sIn.i] != 0) {
		pNew = SafeMalloc(sizeof(Token));
		nErr += GetBigToken(&sIn, pNew, pTable);
		debug3(TOKENIZER, "Token on line %d: [%.*s]\n", pNew->nLine,
		       pNew->nText < 50 ? pNew->nText : 50, pNew->zText);
		if (pFirst == 0) {
			pFirst = pLast = pNew;
			pNew->pPrev = 0;
		} else {
			pLast->pNext = pNew;
			pNew->pPrev = pLast;
			pLast = pNew;
		}
		if (pNew->eType == TT_EOF)
			break;
	}
	if (pLast)
		pLast->pNext = 0;
	blockComment = 0;
	if (nErr) {
		FreeTokenList(pFirst);
		pFirst = 0;
	}

	return pFirst;
}

#if TEST == 1
/*
** Use the following routine to test or debug the tokenizer.
*/
void main(int argc, char **argv)
{
	char *zFile;
	Token *pList, *p;
	IdentTable sTable;

	if (argc != 2) {
		fprintf(stderr, "Usage: %s filename\n", *argv);
		exit(1);
	}
	memset(&sTable, 0, sizeof(sTable));
	zFile = ReadFile(argv[1]);
	if (zFile == 0) {
		fprintf(stderr, "Can't read file \"%s\"\n", argv[1]);
		exit(1);
	}
	pList = TokenizeFile(zFile, &sTable);
	for (p = pList; p; p = p->pNext) {
		int j;
		switch (p->eType) {
		case TT_Space:
			printf("%4d: Space\n", p->nLine);
			break;
		case TT_Id:
			printf("%4d: Id           %.*s\n", p->nLine, p->nText,
			       p->zText);
			break;
		case TT_Preprocessor:
			printf("%4d: Preprocessor %.*s\n", p->nLine, p->nText,
			       p->zText);
			break;
		case TT_Comment:
			printf("%4d: Comment\n", p->nLine);
			break;
		case TT_BlockComment:
			printf("%4d: Block Comment\n", p->nLine);
			break;
		case TT_Number:
			printf("%4d: Number       %.*s\n", p->nLine, p->nText,
			       p->zText);
			break;
		case TT_String:
			printf("%4d: String       %.*s\n", p->nLine, p->nText,
			       p->zText);
			break;
		case TT_Other:
			printf("%4d: Other        %.*s\n", p->nLine, p->nText,
			       p->zText);
			break;
		case TT_Braces:
			for (j = 0;
			     j < p->nText && j < 30 && p->zText[j] != '\n';
			     j++) {
			}
			printf("%4d: Braces       %.*s...}\n", p->nLine, j,
			       p->zText);
			break;
		case TT_EOF:
			printf("%4d: End of file\n", p->nLine);
			break;
		default:
			printf("%4d: type %d\n", p->nLine, p->eType);
			break;
		}
	}
	FreeTokenList(pList);
	SafeFree(zFile);
	IdentTablePrint(&sTable, stdout);
}
#endif

#ifdef DEBUG
/*
** For debugging purposes, write out a list of tokens.
*/
static void PrintTokens(Token *pFirst, Token *pLast)
{
	int needSpace = 0;
	int c;

	pLast = pLast->pNext;
	while (pFirst != pLast) {
		switch (pFirst->eType) {
		case TT_Preprocessor:
			printf("\n%.*s\n", pFirst->nText, pFirst->zText);
			needSpace = 0;
			break;

		case TT_Id:
		case TT_Number:
			printf("%s%.*s", needSpace ? " " : "", pFirst->nText,
			       pFirst->zText);
			needSpace = 1;
			break;

		default:
			c = pFirst->zText[0];
			printf("%s%.*s",
			       (needSpace && (c == '*' || c == '{')) ? " " : "",
			       pFirst->nText, pFirst->zText);
			needSpace = pFirst->zText[0] == ',';
			break;
		}
		pFirst = pFirst->pNext;
	}
}
#endif

/*
** Convert a sequence of tokens into a string and return a pointer
** to that string.  Space to hold the string is obtained from malloc()
** and must be freed by the calling function.
**
** Certain keywords (EXPORT, PRIVATE, PUBLIC, PROTECTED) are always
** skipped.
**
** If pSkip!=0 then skip over nSkip tokens beginning with pSkip.
**
** If zTerm!=0 then append the text to the end.
*/
static char *TokensToString(Token *pFirst, /* First token in the string */
			    Token *pLast, /* Last token in the string */
			    char *zTerm, /* Terminate the string with this text
					    if not NULL */
			    Token *pSkip, /* Skip this token if not NULL */
			    int nSkip /* Skip a total of this many tokens */
)
{
	char *zReturn;
	String str;
	int needSpace = 0;
	int c;
	int iSkip = 0;
	int skipOne = 0;

	StringInit(&str);
	pLast = pLast->pNext;
	while (pFirst != pLast) {
		if (pFirst == pSkip) {
			iSkip = nSkip;
		}
		if (iSkip > 0) {
			iSkip--;
			pFirst = pFirst->pNext;
			continue;
		}
		switch (pFirst->eType) {
		case TT_Preprocessor:
			StringAppend(&str, "\n", 1);
			StringAppend(&str, pFirst->zText, pFirst->nText);
			StringAppend(&str, "\n", 1);
			needSpace = 0;
			break;

		case TT_Id:
			switch (pFirst->zText[0]) {
			case 'E':
				if (pFirst->nText == 6 &&
				    strncmp(pFirst->zText, "EXPORT", 6) == 0) {
					skipOne = 1;
				}
				break;
			case 'P':
				switch (pFirst->nText) {
				case 6:
					skipOne = !strncmp(pFirst->zText,
							   "PUBLIC", 6);
					break;
				case 7:
					skipOne = !strncmp(pFirst->zText,
							   "PRIVATE", 7);
					break;
				case 9:
					skipOne = !strncmp(pFirst->zText,
							   "PROTECTED", 9);
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
			if (skipOne) {
				pFirst = pFirst->pNext;
				skipOne = 0;
				continue;
			}
			/* Fall thru to the next case */
		case TT_Number:
			if (needSpace) {
				StringAppend(&str, " ", 1);
			}
			StringAppend(&str, pFirst->zText, pFirst->nText);
			needSpace = 1;
			break;

		default:
			c = pFirst->zText[0];
			if (needSpace && (c == '*' || c == '{')) {
				StringAppend(&str, " ", 1);
			}
			StringAppend(&str, pFirst->zText, pFirst->nText);
			/* needSpace = pFirst->zText[0]==','; */
			needSpace = 0;
			break;
		}
		pFirst = pFirst->pNext;
	}
	if (zTerm && *zTerm) {
		StringAppend(&str, zTerm, strlen(zTerm));
	}
	zReturn = StrDup(StringGet(&str), 0);
	StringReset(&str);
	return zReturn;
}

/*
** This routine is called when we see one of the keywords "struct",
** "enum", "union" or "class".  This might be the beginning of a
** type declaration.  This routine will process the declaration and
** remove the declaration tokens from the input stream.
**
** If this is a type declaration that is immediately followed by a
** semicolon (in other words it isn't also a variable definition)
** then set *pReset to ';'.  Otherwise leave *pReset at 0.  The
** *pReset flag causes the parser to skip ahead to the next token
** that begins with the value placed in the *pReset flag, if that
** value is different from 0.
*/
static int ProcessTypeDecl(Token *pList, int flags, int *pReset)
{
	Token *pName, *pEnd;
	Decl *pDecl;
	String str;
	int need_to_collapse = 1;
	int type = 0;

	*pReset = 0;
	if (pList == 0 || pList->pNext == 0 || pList->pNext->eType != TT_Id) {
		return 0;
	}
	pName = pList->pNext;

	/* Catch the case of "struct Foo;" and skip it. */
	if (pName->pNext && pName->pNext->zText[0] == ';') {
		*pReset = ';';
		return 0;
	}

	for (pEnd = pName->pNext; pEnd && pEnd->eType != TT_Braces;
	     pEnd = pEnd->pNext) {
		switch (pEnd->zText[0]) {
		case '(':
		case ')':
		case '*':
		case '[':
		case '=':
		case ';':
			return 0;
		}
	}
	if (pEnd == 0) {
		return 0;
	}

	/*
	** At this point, we know we have a type declaration that is bounded
	** by pList and pEnd and has the name pName.
	*/

	/*
	** If the braces are followed immediately by a semicolon, then we are
	** dealing a type declaration only.  There is not variable definition
	** following the type declaration.  So reset...
	*/
	if (pEnd->pNext == 0 || pEnd->pNext->zText[0] == ';') {
		*pReset = ';';
		need_to_collapse = 0;
	} else {
		need_to_collapse = 1;
	}

	if (proto_static == 0 &&
	    (flags & (PS_Local | PS_Export | PS_Interface)) == 0) {
		/* Ignore these objects unless they are explicitly declared as
		*interface,
		** or unless the "-local" command line option was specified. */
		*pReset = ';';
		return 0;
	}

#ifdef DEBUG
	if (debugMask & PARSER) {
		printf("**** Found type: %.*s %.*s...\n", pList->nText,
		       pList->zText, pName->nText, pName->zText);
		PrintTokens(pList, pEnd);
		printf(";\n");
	}
#endif

	/*
	** Create a new Decl object for this definition.  Actually, if this
	** is a C++ class definition, then the Decl object might already exist,
	** so check first for that case before creating a new one.
	*/
	switch (*pList->zText) {
	case 'c':
		type = TY_Class;
		break;
	case 's':
		type = TY_Structure;
		break;
	case 'e':
		type = TY_Enumeration;
		break;
	case 'u':
		type = TY_Union;
		break;
	default: /* Can't Happen */
		break;
	}
	if (type != TY_Class) {
		pDecl = 0;
	} else {
		pDecl = FindDecl(pName->zText, pName->nText);
		if (pDecl && (pDecl->flags & type) != type)
			pDecl = 0;
	}
	if (pDecl == 0) {
		pDecl = CreateDecl(pName->zText, pName->nText);
	}
	if ((flags & PS_Static) || !(flags & (PS_Interface | PS_Export))) {
		DeclSetProperty(pDecl, DP_Local);
	}
	DeclSetProperty(pDecl, type);

	/* The object has a full declaration only if it is contained within
	** "#if INTERFACE...#endif" or "#if EXPORT_INTERFACE...#endif" or
	** "#if LOCAL_INTERFACE...#endif".  Otherwise, we only give it a
	** forward declaration.
	*/
	if (flags & (PS_Local | PS_Export | PS_Interface)) {
		pDecl->zDecl = TokensToString(pList, pEnd, ";\n", 0, 0);
	} else {
		pDecl->zDecl = 0;
	}
	pDecl->pComment = pList->pComment;
	StringInit(&str);
	StringAppend(&str, "typedef ", 0);
	StringAppend(&str, pList->zText, pList->nText);
	StringAppend(&str, " ", 0);
	StringAppend(&str, pName->zText, pName->nText);
	StringAppend(&str, " ", 0);
	StringAppend(&str, pName->zText, pName->nText);
	StringAppend(&str, ";\n", 2);
	pDecl->zFwd = StrDup(StringGet(&str), 0);
	StringReset(&str);
	StringInit(&str);
	StringAppend(&str, pList->zText, pList->nText);
	StringAppend(&str, " ", 0);
	StringAppend(&str, pName->zText, pName->nText);
	StringAppend(&str, ";\n", 2);
	pDecl->zFwdCpp = StrDup(StringGet(&str), 0);
	StringReset(&str);
	if (flags & PS_Export) {
		DeclSetProperty(pDecl, DP_Export);
	} else if (flags & PS_Local) {
		DeclSetProperty(pDecl, DP_Local);
	}

	/* Here's something weird.  ANSI-C doesn't allow a forward declaration
	** of an enumeration.  So we have to build the typedef into the
	** definition.
	*/
	if (pDecl->zDecl && DeclHasProperty(pDecl, TY_Enumeration)) {
		StringInit(&str);
		StringAppend(&str, pDecl->zDecl, 0);
		StringAppend(&str, pDecl->zFwd, 0);
		SafeFree(pDecl->zDecl);
		SafeFree(pDecl->zFwd);
		pDecl->zFwd = 0;
		pDecl->zDecl = StrDup(StringGet(&str), 0);
		StringReset(&str);
	}

	if (pName->pNext->zText[0] == ':') {
		DeclSetProperty(pDecl, DP_Cplusplus);
	}
	if (pName->nText == 5 && strncmp(pName->zText, "class", 5) == 0) {
		DeclSetProperty(pDecl, DP_Cplusplus);
	}

	/*
	** Remove all but pList and pName from the input stream.
	*/
	if (need_to_collapse) {
		while (pEnd != pName) {
			Token *pPrev = pEnd->pPrev;
			pPrev->pNext = pEnd->pNext;
			pEnd->pNext->pPrev = pPrev;
			SafeFree(pEnd);
			pEnd = pPrev;
		}
	}
	return 0;
}

/*
** Given a list of tokens that declare something (a function, procedure,
** variable or typedef) find the token which contains the name of the
** thing being declared.
**
** Algorithm:
**
**   The name is:
**
**     1.  The first identifier that is followed by a "[", or
**
**     2.  The first identifier that is followed by a "(" where the
**         "(" is followed by another identifier, or
**
**     3.  The first identifier followed by "::", or
**
**     4.  If none of the above, then the last identifier.
**
**   In all of the above, certain reserved words (like "char") are
**   not considered identifiers.
*/
static Token *FindDeclName(Token *pFirst, Token *pLast)
{
	Token *pName = 0;
	Token *p;
	int c;

	if (pFirst == 0 || pLast == 0) {
		return 0;
	}
	pLast = pLast->pNext;
	for (p = pFirst; p && p != pLast; p = p->pNext) {
		if (p->eType == TT_Id) {
			static IdentTable sReserved;
			static int isInit = 0;
			static const char *aWords[] = {
				"char",	     "class",	 "const",    "double",
				"enum",	     "extern",	 "EXPORT",   "ET_PROC",
				"float",     "int",	 "long",     "PRIVATE",
				"PROTECTED", "PUBLIC",	 "register", "static",
				"struct",    "sizeof",	 "signed",   "typedef",
				"union",     "volatile", "virtual",  "void",
			};

			if (!isInit) {
				int i;
				for (i = 0;
				     i < sizeof(aWords) / sizeof(aWords[0]);
				     i++) {
					IdentTableInsert(&sReserved, aWords[i],
							 0);
				}
				isInit = 1;
			}
			if (!IdentTableTest(&sReserved, p->zText, p->nText)) {
				pName = p;
			}
		} else if (p == pFirst) {
			continue;
		} else if ((c = p->zText[0]) == '[' && pName) {
			break;
		} else if (c == '(' && p->pNext && p->pNext->eType == TT_Id &&
			   pName) {
			break;
		} else if (c == ':' && p->zText[1] == ':' && pName) {
			break;
		}
	}
	return pName;
}

/*
** This routine is called when we see a method for a class that begins
** with the PUBLIC, PRIVATE, or PROTECTED keywords.  Such methods are
** added to their class definitions.
*/
static int ProcessMethodDef(Token *pFirst, Token *pLast, int flags)
{
	Token *pClass;
	char *zDecl;
	Decl *pDecl;
	String str;
	int type;

	pLast = pLast->pPrev;
	while (pFirst->zText[0] == 'P') {
		int rc = 1;
		switch (pFirst->nText) {
		case 6:
			rc = strncmp(pFirst->zText, "PUBLIC", 6);
			break;
		case 7:
			rc = strncmp(pFirst->zText, "PRIVATE", 7);
			break;
		case 9:
			rc = strncmp(pFirst->zText, "PROTECTED", 9);
			break;
		default:
			break;
		}
		if (rc)
			break;
		pFirst = pFirst->pNext;
	}
	pClass = FindDeclName(pFirst, pLast);
	if (pClass == 0) {
		fprintf(stderr,
			"%s:%d: Unable to find the class name for this method\n",
			zFilename, pFirst->nLine);
		return 1;
	}
	pDecl = FindDecl(pClass->zText, pClass->nText);
	if (pDecl == 0 || (pDecl->flags & TY_Class) != TY_Class) {
		pDecl = CreateDecl(pClass->zText, pClass->nText);
		DeclSetProperty(pDecl, TY_Class);
	}
	StringInit(&str);
	if (pDecl->zExtra) {
		StringAppend(&str, pDecl->zExtra, 0);
		SafeFree(pDecl->zExtra);
		pDecl->zExtra = 0;
	}
	type = flags & PS_PPP;
	if (pDecl->extraType != type) {
		if (type & PS_Public) {
			StringAppend(&str, "public:\n", 0);
			pDecl->extraType = PS_Public;
		} else if (type & PS_Protected) {
			StringAppend(&str, "protected:\n", 0);
			pDecl->extraType = PS_Protected;
		} else if (type & PS_Private) {
			StringAppend(&str, "private:\n", 0);
			pDecl->extraType = PS_Private;
		}
	}
	StringAppend(&str, "  ", 0);
	zDecl = TokensToString(pFirst, pLast, ";\n", pClass, 2);
	if (strncmp(zDecl, pClass->zText, pClass->nText) == 0) {
		/* If member initializer list is found after a constructor,
		** skip that part. */
		char *colon = strchr(zDecl, ':');
		if (colon != 0 && colon[1] != 0) {
			*colon++ = ';';
			*colon++ = '\n';
			*colon = 0;
		}
	}
	StringAppend(&str, zDecl, 0);
	SafeFree(zDecl);
	pDecl->zExtra = StrDup(StringGet(&str), 0);
	StringReset(&str);
	return 0;
}

/*
** This routine is called when we see a function or procedure definition.
** We make an entry in the declaration table that is a prototype for this
** function or procedure.
*/
static int ProcessProcedureDef(Token *pFirst, Token *pLast, int flags)
{
	Token *pName;
	Decl *pDecl;
	Token *pCode;

	if (pFirst == 0 || pLast == 0) {
		return 0;
	}
	if (flags & PS_Method) {
		if (flags & PS_PPP) {
			return ProcessMethodDef(pFirst, pLast, flags);
		} else {
			return 0;
		}
	}
	if ((flags & PS_Static) != 0 && !proto_static) {
		return 0;
	}
	pCode = pLast;
	while (pLast && pLast != pFirst && pLast->zText[0] != ')') {
		pLast = pLast->pPrev;
	}
	if (pLast == 0 || pLast == pFirst || pFirst->pNext == pLast) {
		fprintf(stderr, "%s:%d: Unrecognized syntax.\n", zFilename,
			pFirst->nLine);
		return 1;
	}
	if (flags & (PS_Interface | PS_Export | PS_Local)) {
		fprintf(stderr,
			"%s:%d: Missing \"inline\" on function or procedure.\n",
			zFilename, pFirst->nLine);
		return 1;
	}
	pName = FindDeclName(pFirst, pLast);
	if (pName == 0) {
		fprintf(stderr,
			"%s:%d: Malformed function or procedure definition.\n",
			zFilename, pFirst->nLine);
		return 1;
	}
	if (strncmp(pName->zText, "main", pName->nText) == 0) {
		/* skip main() decl. */
		return 0;
	}
	/*
	** At this point we've isolated a procedure declaration between pFirst
	** and pLast with the name pName.
	*/
#ifdef DEBUG
	if (debugMask & PARSER) {
		printf("**** Found routine: %.*s on line %d...\n", pName->nText,
		       pName->zText, pFirst->nLine);
		PrintTokens(pFirst, pLast);
		printf(";\n");
	}
#endif
	pDecl = CreateDecl(pName->zText, pName->nText);
	pDecl->pComment = pFirst->pComment;
	if (pCode && pCode->eType == TT_Braces) {
		pDecl->tokenCode = *pCode;
	}
	DeclSetProperty(pDecl, TY_Subroutine);
	pDecl->zDecl = TokensToString(pFirst, pLast, ";\n", 0, 0);
	if ((flags & (PS_Static | PS_Local2)) != 0) {
		DeclSetProperty(pDecl, DP_Local);
	} else if ((flags & (PS_Export2)) != 0) {
		DeclSetProperty(pDecl, DP_Export);
	}

	if (flags & DP_Cplusplus) {
		DeclSetProperty(pDecl, DP_Cplusplus);
	} else {
		DeclSetProperty(pDecl, DP_ExternCReqd);
	}

	return 0;
}

/*
** This routine is called whenever we see the "inline" keyword.  We
** need to seek-out the inline function or procedure and make a
** declaration out of the entire definition.
*/
static int ProcessInlineProc(Token *pFirst, int flags, int *pReset)
{
	Token *pName;
	Token *pEnd;
	Decl *pDecl;

	for (pEnd = pFirst; pEnd; pEnd = pEnd->pNext) {
		if (pEnd->zText[0] == '{' || pEnd->zText[0] == ';') {
			*pReset = pEnd->zText[0];
			break;
		}
	}
	if (pEnd == 0) {
		*pReset = ';';
		fprintf(stderr,
			"%s:%d: incomplete inline procedure definition\n",
			zFilename, pFirst->nLine);
		return 1;
	}
	pName = FindDeclName(pFirst, pEnd);
	if (pName == 0) {
		fprintf(stderr,
			"%s:%d: malformed inline procedure definition\n",
			zFilename, pFirst->nLine);
		return 1;
	}

#ifdef DEBUG
	if (debugMask & PARSER) {
		printf("**** Found inline routine: %.*s on line %d...\n",
		       pName->nText, pName->zText, pFirst->nLine);
		PrintTokens(pFirst, pEnd);
		printf("\n");
	}
#endif
	pDecl = CreateDecl(pName->zText, pName->nText);
	pDecl->pComment = pFirst->pComment;
	DeclSetProperty(pDecl, TY_Subroutine);
	pDecl->zDecl = TokensToString(pFirst, pEnd, ";\n", 0, 0);
	if ((flags & (PS_Static | PS_Local | PS_Local2))) {
		DeclSetProperty(pDecl, DP_Local);
	} else if (flags & (PS_Export | PS_Export2)) {
		DeclSetProperty(pDecl, DP_Export);
	}

	if (flags & DP_Cplusplus) {
		DeclSetProperty(pDecl, DP_Cplusplus);
	} else {
		DeclSetProperty(pDecl, DP_ExternCReqd);
	}

	return 0;
}

/*
** Determine if the tokens between pFirst and pEnd form a variable
** definition or a function prototype.  Return TRUE if we are dealing
** with a variable defintion and FALSE for a prototype.
**
** pEnd is the token that ends the object.  It can be either a ';' or
** a '='.  If it is '=', then assume we have a variable definition.
**
** If pEnd is ';', then the determination is more difficult.  We have
** to search for an occurrence of an ID followed immediately by '('.
** If found, we have a prototype.  Otherwise we are dealing with a
** variable definition.
*/
static int isVariableDef(Token *pFirst, Token *pEnd)
{
	if (pEnd && pEnd->zText[0] == '=' &&
	    (pEnd->pPrev->nText != 8 ||
	     strncmp(pEnd->pPrev->zText, "operator", 8) != 0)) {
		return 1;
	}
	while (pFirst && pFirst != pEnd && pFirst->pNext &&
	       pFirst->pNext != pEnd) {
		if (pFirst->eType == TT_Id && pFirst->pNext->zText[0] == '(') {
			return 0;
		}
		pFirst = pFirst->pNext;
	}
	return 1;
}

/*
** Return TRUE if pFirst is the first token of a static assert.
*/
static int isStaticAssert(Token *pFirst)
{
	if ((pFirst->nText == 13 &&
	     strncmp(pFirst->zText, "static_assert", 13) == 0) ||
	    (pFirst->nText == 14 &&
	     strncmp(pFirst->zText, "_Static_assert", 14) == 0)) {
		return 1;
	} else {
		return 0;
	}
}

/*
** This routine is called whenever we encounter a ";" or "=".  The stuff
** between pFirst and pLast constitutes either a typedef or a global
** variable definition.  Do the right thing.
*/
static int ProcessDecl(Token *pFirst, Token *pEnd, int flags)
{
	Token *pName;
	Decl *pDecl;
	int isLocal = 0;
	int isVar;
	int nErr = 0;

	if (pFirst == 0 || pEnd == 0) {
		return 0;
	}
	if (flags & PS_Typedef) {
		if ((flags & (PS_Export2 | PS_Local2)) != 0) {
			fprintf(stderr,
				"%s:%d: \"EXPORT\" or \"LOCAL\" ignored before typedef.\n",
				zFilename, pFirst->nLine);
			nErr++;
		}
		if ((flags & (PS_Interface | PS_Export | PS_Local |
			      DP_Cplusplus)) == 0) {
			/* It is illegal to duplicate a typedef in C (but OK in
			*C++).
			** So don't record typedefs that aren't within a C++
			*file or
			** within #if INTERFACE..#endif */
			return nErr;
		}
		if ((flags & (PS_Interface | PS_Export | PS_Local)) == 0 &&
		    proto_static == 0) {
			/* Ignore typedefs that are not with "#if
			*INTERFACE..#endif" unless
			** the "-local" command line option is used. */
			return nErr;
		}
		if ((flags & (PS_Interface | PS_Export)) == 0) {
			/* typedefs are always local, unless within #if
			 * INTERFACE..#endif */
			isLocal = 1;
		}
	} else if (flags & (PS_Static | PS_Local2)) {
		if (proto_static == 0 && (flags & PS_Local2) == 0) {
			/* Don't record static variables unless the "-local"
			*command line
			** option was specified or the "LOCAL" keyword is used.
		       */
			return nErr;
		}
		while (pFirst != 0 && pFirst->pNext != pEnd &&
		       ((pFirst->nText == 6 &&
			 strncmp(pFirst->zText, "static", 6) == 0) ||
			(pFirst->nText == 5 &&
			 strncmp(pFirst->zText, "LOCAL", 6) == 0))) {
			/* Lose the initial "static" or local from local
			*variables.
			** We'll prepend "extern" later. */
			pFirst = pFirst->pNext;
			isLocal = 1;
		}
		if (pFirst == 0 || !isLocal) {
			return nErr;
		}
	} else if (flags & PS_Method) {
		/* Methods are declared by their class.  Don't declare
		 * separately. */
		return nErr;
	} else if (isStaticAssert(pFirst)) {
		return 0;
	}
	isVar = (flags & (PS_Typedef | PS_Method)) == 0 &&
		isVariableDef(pFirst, pEnd);
	if (isVar && (flags & (PS_Interface | PS_Export | PS_Local)) != 0 &&
	    (flags & PS_Extern) == 0) {
		fprintf(stderr,
			"%s:%d: Can't define a variable in this context\n",
			zFilename, pFirst->nLine);
		nErr++;
	}
	pName = FindDeclName(pFirst, pEnd->pPrev);
	if (pName == 0) {
		if (pFirst->nText == 4 &&
		    strncmp(pFirst->zText, "enum", 4) == 0) {
			/* Ignore completely anonymous enums.  See documentation
			 * section 3.8.1. */
			return nErr;
		} else {
			fprintf(stderr,
				"%s:%d: Can't find a name for the object declared here.\n",
				zFilename, pFirst->nLine);
			return nErr + 1;
		}
	}

#ifdef DEBUG
	if (debugMask & PARSER) {
		if (flags & PS_Typedef) {
			printf("**** Found typedef %.*s at line %d...\n",
			       pName->nText, pName->zText, pName->nLine);
		} else if (isVar) {
			printf("**** Found variable %.*s at line %d...\n",
			       pName->nText, pName->zText, pName->nLine);
		} else {
			printf("**** Found prototype %.*s at line %d...\n",
			       pName->nText, pName->zText, pName->nLine);
		}
		PrintTokens(pFirst, pEnd->pPrev);
		printf(";\n");
	}
#endif

	pDecl = CreateDecl(pName->zText, pName->nText);
	if ((flags & PS_Typedef)) {
		DeclSetProperty(pDecl, TY_Typedef);
	} else if (isVar) {
		DeclSetProperty(pDecl, DP_ExternReqd | TY_Variable);
		if (!(flags & DP_Cplusplus)) {
			DeclSetProperty(pDecl, DP_ExternCReqd);
		}
	} else {
		DeclSetProperty(pDecl, TY_Subroutine);
		if (!(flags & DP_Cplusplus)) {
			DeclSetProperty(pDecl, DP_ExternCReqd);
		}
	}
	pDecl->pComment = pFirst->pComment;
	pDecl->zDecl = TokensToString(pFirst, pEnd->pPrev, ";\n", 0, 0);
	if (isLocal || (flags & (PS_Local | PS_Local2)) != 0) {
		DeclSetProperty(pDecl, DP_Local);
	} else if (flags & (PS_Export | PS_Export2)) {
		DeclSetProperty(pDecl, DP_Export);
	}
	if (flags & DP_Cplusplus) {
		DeclSetProperty(pDecl, DP_Cplusplus);
	}
	return nErr;
}

/*
** Push an if condition onto the if stack
*/
static void PushIfMacro(const char *zPrefix, /* A prefix, like "define" or "!"
					      */
			const char *zText, /* The condition */
			int nText, /* Number of characters in zText */
			int nLine, /* Line number where this macro occurs */
			int flags /* Either 0, PS_Interface, PS_Export or
				     PS_Local */
)
{
	Ifmacro *pIf;
	int nByte;

	nByte = sizeof(Ifmacro);
	if (zText) {
		if (zPrefix) {
			nByte += strlen(zPrefix) + 2;
		}
		nByte += nText + 1;
	}
	pIf = SafeMalloc(nByte);
	if (zText) {
		pIf->zCondition = (char *)&pIf[1];
		if (zPrefix) {
			sprintf(pIf->zCondition, "%s(%.*s)", zPrefix, nText,
				zText);
		} else {
			sprintf(pIf->zCondition, "%.*s", nText, zText);
		}
	} else {
		pIf->zCondition = 0;
	}
	pIf->nLine = nLine;
	pIf->flags = flags;
	pIf->pNext = ifStack;
	ifStack = pIf;
}

/*
** This routine is called to handle all preprocessor directives.
**
** This routine will recompute the value of *pPresetFlags to be the
** logical or of all flags on all nested #ifs.  The #ifs that set flags
** are as follows:
**
**        conditional                   flag set
**        ------------------------      --------------------
**        #if INTERFACE                 PS_Interface
**        #if EXPORT_INTERFACE          PS_Export
**        #if LOCAL_INTERFACE           PS_Local
**
** For example, if after processing the preprocessor token given
** by pToken there is an "#if INTERFACE" on the preprocessor
** stack, then *pPresetFlags will be set to PS_Interface.
*/
static int ParsePreprocessor(Token *pToken, int flags, int *pPresetFlags)
{
	const char *zCmd;
	int nCmd;
	const char *zArg;
	int nArg;
	int nErr = 0;
	Ifmacro *pIf;

	zCmd = &pToken->zText[1];
	while (isspace(*zCmd) && *zCmd != '\n') {
		zCmd++;
	}
	if (!isalpha(*zCmd)) {
		return 0;
	}
	nCmd = 1;
	while (isalpha(zCmd[nCmd])) {
		nCmd++;
	}

	if (nCmd == 5 && strncmp(zCmd, "endif", 5) == 0) {
		/*
		** Pop the if stack
		*/
		pIf = ifStack;
		if (pIf == 0) {
			fprintf(stderr, "%s:%d: extra '#endif'.\n", zFilename,
				pToken->nLine);
			return 1;
		}
		ifStack = pIf->pNext;
		SafeFree(pIf);
	} else if (nCmd == 6 && strncmp(zCmd, "define", 6) == 0) {
		/*
		** Record a #define if we are in PS_Interface or PS_Export
		*/
		Decl *pDecl;
		if (!(flags & (PS_Local | PS_Interface | PS_Export))) {
			return 0;
		}
		zArg = &zCmd[6];
		while (*zArg && isspace(*zArg) && *zArg != '\n') {
			zArg++;
		}
		if (*zArg == 0 || *zArg == '\n') {
			return 0;
		}
		for (nArg = 0; ISALNUM(zArg[nArg]); nArg++) {
		}
		if (nArg == 0) {
			return 0;
		}
		pDecl = CreateDecl(zArg, nArg);
		pDecl->pComment = pToken->pComment;
		DeclSetProperty(pDecl, TY_Macro);
		pDecl->zDecl = SafeMalloc(pToken->nText + 2);
		sprintf(pDecl->zDecl, "%.*s\n", pToken->nText, pToken->zText);
		if (flags & PS_Export) {
			DeclSetProperty(pDecl, DP_Export);
		} else if (flags & PS_Local) {
			DeclSetProperty(pDecl, DP_Local);
		}
	} else if (nCmd == 7 && strncmp(zCmd, "include", 7) == 0) {
		/*
		** Record an #include if we are in PS_Interface or PS_Export
		*/
		Include *pInclude;
		char *zIf;

		if (!(flags & (PS_Interface | PS_Export))) {
			return 0;
		}
		zArg = &zCmd[7];
		while (*zArg && isspace(*zArg)) {
			zArg++;
		}
		for (nArg = 0; !isspace(zArg[nArg]); nArg++) {
		}
		if ((zArg[0] == '"' && zArg[nArg - 1] != '"') ||
		    (zArg[0] == '<' && zArg[nArg - 1] != '>')) {
			fprintf(stderr,
				"%s:%d: malformed #include statement.\n",
				zFilename, pToken->nLine);
			return 1;
		}
		zIf = GetIfString();
		if (zIf) {
			pInclude = SafeMalloc(sizeof(Include) + nArg * 2 +
					      strlen(zIf) + 10);
			pInclude->zFile = (char *)&pInclude[1];
			pInclude->zLabel = &pInclude->zFile[nArg + 1];
			sprintf(pInclude->zFile, "%.*s", nArg, zArg);
			sprintf(pInclude->zLabel, "%.*s:%s", nArg, zArg, zIf);
			pInclude->zIf = &pInclude->zLabel[nArg + 1];
			SafeFree(zIf);
		} else {
			pInclude = SafeMalloc(sizeof(Include) + nArg + 1);
			pInclude->zFile = (char *)&pInclude[1];
			sprintf(pInclude->zFile, "%.*s", nArg, zArg);
			pInclude->zIf = 0;
			pInclude->zLabel = pInclude->zFile;
		}
		pInclude->pNext = includeList;
		includeList = pInclude;
	} else if (nCmd == 2 && strncmp(zCmd, "if", 2) == 0) {
		/*
		** Push an #if.  Watch for the special cases of INTERFACE
		** and EXPORT_INTERFACE and LOCAL_INTERFACE
		*/
		zArg = &zCmd[2];
		while (*zArg && isspace(*zArg) && *zArg != '\n') {
			zArg++;
		}
		if (*zArg == 0 || *zArg == '\n') {
			return 0;
		}
		nArg = pToken->nText + (int)(pToken->zText - zArg);
		if (pToken->zText[pToken->nText - 1] == '\r') {
			nArg--;
		}
		if (nArg == 9 && strncmp(zArg, "INTERFACE", 9) == 0) {
			PushIfMacro(0, 0, 0, pToken->nLine, PS_Interface);
		} else if (nArg == 16 &&
			   strncmp(zArg, "EXPORT_INTERFACE", 16) == 0) {
			PushIfMacro(0, 0, 0, pToken->nLine, PS_Export);
		} else if (nArg == 15 &&
			   strncmp(zArg, "LOCAL_INTERFACE", 15) == 0) {
			PushIfMacro(0, 0, 0, pToken->nLine, PS_Local);
		} else if (nArg == 15 &&
			   strncmp(zArg, "MAKEHEADERS_STOPLOCAL_INTERFACE",
				   15) == 0) {
			PushIfMacro(0, 0, 0, pToken->nLine, PS_Local);
		} else {
			PushIfMacro(0, zArg, nArg, pToken->nLine, 0);
		}
	} else if (nCmd == 5 && strncmp(zCmd, "ifdef", 5) == 0) {
		/*
		** Push an #ifdef.
		*/
		zArg = &zCmd[5];
		while (*zArg && isspace(*zArg) && *zArg != '\n') {
			zArg++;
		}
		if (*zArg == 0 || *zArg == '\n') {
			return 0;
		}
		nArg = pToken->nText + (int)(pToken->zText - zArg);
		if (pToken->zText[pToken->nText - 1] == '\r') {
			nArg--;
		}
		PushIfMacro("defined", zArg, nArg, pToken->nLine, 0);
	} else if (nCmd == 6 && strncmp(zCmd, "ifndef", 6) == 0) {
		/*
		** Push an #ifndef.
		*/
		zArg = &zCmd[6];
		while (*zArg && isspace(*zArg) && *zArg != '\n') {
			zArg++;
		}
		if (*zArg == 0 || *zArg == '\n') {
			return 0;
		}
		nArg = pToken->nText + (int)(pToken->zText - zArg);
		if (pToken->zText[pToken->nText - 1] == '\r') {
			nArg--;
		}
		PushIfMacro("!defined", zArg, nArg, pToken->nLine, 0);
	} else if (nCmd == 4 && strncmp(zCmd, "else", 4) == 0) {
		/*
		** Invert the #if on the top of the stack
		*/
		if (ifStack == 0) {
			fprintf(stderr, "%s:%d: '#else' without an '#if'\n",
				zFilename, pToken->nLine);
			return 1;
		}
		pIf = ifStack;
		if (pIf->zCondition) {
			ifStack = ifStack->pNext;
			PushIfMacro("!", pIf->zCondition,
				    strlen(pIf->zCondition), pIf->nLine, 0);
			SafeFree(pIf);
		} else {
			pIf->flags = 0;
		}
	} else {
		/*
		** This directive can be safely ignored
		*/
		return 0;
	}

	/*
	** Recompute the preset flags
	*/
	*pPresetFlags = 0;
	for (pIf = ifStack; pIf; pIf = pIf->pNext) {
		*pPresetFlags |= pIf->flags;
	}

	return nErr;
}

/*
** Parse an entire file.  Return the number of errors.
**
** pList is a list of tokens in the file.  Whitespace tokens have been
** eliminated, and text with {...} has been collapsed into a
** single TT_Brace token.
**
** initFlags are a set of parse flags that should always be set for this
** file.  For .c files this is normally 0.  For .h files it is PS_Interface.
*/
static int ParseFile(Token *pList, int initFlags)
{
	int nErr = 0;
	Token *pStart = 0;
	int flags = initFlags;
	int presetFlags = initFlags;
	int resetFlag = 0;

	includeList = 0;
	while (pList) {
		switch (pList->eType) {
		case TT_EOF:
			goto end_of_loop;

		case TT_Preprocessor:
			nErr += ParsePreprocessor(pList, flags, &presetFlags);
			pStart = 0;
			presetFlags |= initFlags;
			flags = presetFlags;
			break;

		case TT_Other:
			switch (pList->zText[0]) {
			case ';':
				nErr += ProcessDecl(pStart, pList, flags);
				pStart = 0;
				flags = presetFlags;
				break;

			case '=':
				if (pList->pPrev->nText == 8 &&
				    strncmp(pList->pPrev->zText, "operator",
					    8) == 0) {
					break;
				}
				nErr += ProcessDecl(pStart, pList, flags);
				pStart = 0;
				while (pList && pList->zText[0] != ';') {
					pList = pList->pNext;
				}
				if (pList == 0)
					goto end_of_loop;
				flags = presetFlags;
				break;

			case ':':
				if (pList->zText[1] == ':') {
					flags |= PS_Method;
				}
				break;

			default:
				break;
			}
			break;

		case TT_Braces:
			nErr += ProcessProcedureDef(pStart, pList, flags);
			pStart = 0;
			flags = presetFlags;
			break;

		case TT_Id:
			if (pStart == 0) {
				pStart = pList;
				flags = presetFlags;
			}
			resetFlag = 0;
			switch (pList->zText[0]) {
			case 'c':
				if (pList->nText == 5 &&
				    strncmp(pList->zText, "class", 5) == 0) {
					nErr += ProcessTypeDecl(pList, flags,
								&resetFlag);
				}
				break;

			case 'E':
				if (pList->nText == 6 &&
				    strncmp(pList->zText, "EXPORT", 6) == 0) {
					flags |= PS_Export2;
					/* pStart = 0; */
				}
				break;

			case 'e':
				if (pList->nText == 4 &&
				    strncmp(pList->zText, "enum", 4) == 0) {
					if (pList->pNext &&
					    pList->pNext->eType == TT_Braces) {
						pList = pList->pNext;
					} else {
						nErr += ProcessTypeDecl(
							pList, flags,
							&resetFlag);
					}
				} else if (pList->nText == 6 &&
					   strncmp(pList->zText, "extern", 6) ==
						   0) {
					pList = pList->pNext;
					if (pList && pList->nText == 3 &&
					    strncmp(pList->zText, "\"C\"", 3) ==
						    0) {
						pList = pList->pNext;
						flags &= ~DP_Cplusplus;
					} else {
						flags |= PS_Extern;
					}
					pStart = pList;
				}
				break;

			case 'i':
				if (pList->nText == 6 &&
				    strncmp(pList->zText, "inline", 6) == 0 &&
				    (flags & PS_Static) == 0) {
					nErr += ProcessInlineProc(pList, flags,
								  &resetFlag);
				}
				break;

			case 'L':
				if (pList->nText == 5 &&
				    strncmp(pList->zText, "LOCAL", 5) == 0) {
					flags |= PS_Local2;
					pStart = pList;
				}
				break;

			case 'P':
				if (pList->nText == 6 &&
				    strncmp(pList->zText, "PUBLIC", 6) == 0) {
					flags |= PS_Public;
					pStart = pList;
				} else if (pList->nText == 7 &&
					   strncmp(pList->zText, "PRIVATE",
						   7) == 0) {
					flags |= PS_Private;
					pStart = pList;
				} else if (pList->nText == 9 &&
					   strncmp(pList->zText, "PROTECTED",
						   9) == 0) {
					flags |= PS_Protected;
					pStart = pList;
				}
				break;

			case 's':
				if (pList->nText == 6 &&
				    strncmp(pList->zText, "struct", 6) == 0) {
					if (pList->pNext &&
					    pList->pNext->eType == TT_Braces) {
						pList = pList->pNext;
					} else {
						nErr += ProcessTypeDecl(
							pList, flags,
							&resetFlag);
					}
				} else if (pList->nText == 6 &&
					   strncmp(pList->zText, "static", 6) ==
						   0) {
					flags |= PS_Static;
				}
				break;

			case 't':
				if (pList->nText == 7 &&
				    strncmp(pList->zText, "typedef", 7) == 0) {
					flags |= PS_Typedef;
				}
				break;

			case 'u':
				if (pList->nText == 5 &&
				    strncmp(pList->zText, "union", 5) == 0) {
					if (pList->pNext &&
					    pList->pNext->eType == TT_Braces) {
						pList = pList->pNext;
					} else {
						nErr += ProcessTypeDecl(
							pList, flags,
							&resetFlag);
					}
				}
				break;

			default:
				break;
			}
			if (resetFlag != 0) {
				while (pList && pList->zText[0] != resetFlag) {
					pList = pList->pNext;
				}
				if (pList == 0)
					goto end_of_loop;
				pStart = 0;
				flags = presetFlags;
			}
			break;

		case TT_String:
		case TT_Number:
			break;

		default:
			pStart = pList;
			flags = presetFlags;
			break;
		}
		pList = pList->pNext;
	}
end_of_loop:

	/* Verify that all #ifs have a matching "#endif" */
	while (ifStack) {
		Ifmacro *pIf = ifStack;
		ifStack = pIf->pNext;
		fprintf(stderr, "%s:%d: This '#if' has no '#endif'\n",
			zFilename, pIf->nLine);
		SafeFree(pIf);
	}

	return nErr;
}

/*
** If the given Decl object has a non-null zExtra field, then the text
** of that zExtra field needs to be inserted in the middle of the
** zDecl field before the last "}" in the zDecl.  This routine does that.
** If the zExtra is NULL, this routine is a no-op.
**
** zExtra holds extra method declarations for classes.  The declarations
** have to be inserted into the class definition.
*/
static void InsertExtraDecl(Decl *pDecl)
{
	int i;
	String str;

	if (pDecl == 0 || pDecl->zExtra == 0 || pDecl->zDecl == 0)
		return;
	i = strlen(pDecl->zDecl) - 1;
	while (i > 0 && pDecl->zDecl[i] != '}') {
		i--;
	}
	StringInit(&str);
	StringAppend(&str, pDecl->zDecl, i);
	StringAppend(&str, pDecl->zExtra, 0);
	StringAppend(&str, &pDecl->zDecl[i], 0);
	SafeFree(pDecl->zDecl);
	SafeFree(pDecl->zExtra);
	pDecl->zDecl = StrDup(StringGet(&str), 0);
	StringReset(&str);
	pDecl->zExtra = 0;
}

/*
** Reset the DP_Forward and DP_Declared flags on all Decl structures.
** Set both flags for anything that is tagged as local and isn't
** in the file zFilename so that it won't be printing in other files.
*/
static void ResetDeclFlags(char *zFilename)
{
	Decl *pDecl;

	for (pDecl = pDeclFirst; pDecl; pDecl = pDecl->pNext) {
		DeclClearProperty(pDecl, DP_Forward | DP_Declared);
		if (DeclHasProperty(pDecl, DP_Local) &&
		    pDecl->zFile != zFilename) {
			DeclSetProperty(pDecl, DP_Forward | DP_Declared);
		}
	}
}

/*
** Forward declaration of the ScanText() function.
*/
static void ScanText(const char *, GenState *pState);

/*
** The output in pStr is currently within an #if CONTEXT where context
** is equal to *pzIf.  (*pzIf might be NULL to indicate that we are
** not within any #if at the moment.)  We are getting ready to output
** some text that needs to be within the context of "#if NEW" where
** NEW is zIf.  Make an appropriate change to the context.
*/
static void ChangeIfContext(const char *zIf, /* The desired #if context */
			    GenState *pState /* Current state of the code
						generator */
)
{
	if (zIf == 0) {
		if (pState->zIf == 0)
			return;
		StringAppend(pState->pStr, "#endif\n", 0);
		pState->zIf = 0;
	} else {
		if (pState->zIf) {
			if (strcmp(zIf, pState->zIf) == 0)
				return;
			StringAppend(pState->pStr, "#endif\n", 0);
			pState->zIf = 0;
		}
		ScanText(zIf, pState);
		if (pState->zIf != 0) {
			StringAppend(pState->pStr, "#endif\n", 0);
		}
		StringAppend(pState->pStr, "#if ", 0);
		StringAppend(pState->pStr, zIf, 0);
		StringAppend(pState->pStr, "\n", 0);
		pState->zIf = zIf;
	}
}

/*
** Add to the string pStr a #include of every file on the list of
** include files pInclude.  The table pTable contains all files that
** have already been #included at least once.  Don't add any
** duplicates.  Update pTable with every new #include that is added.
*/
static void AddIncludes(Include *pInclude, /* Write every #include on this list
					    */
			GenState *pState /* Current state of the code generator
					  */
)
{
	if (pInclude) {
		if (pInclude->pNext) {
			AddIncludes(pInclude->pNext, pState);
		}
		if (IdentTableInsert(pState->pTable, pInclude->zLabel, 0)) {
			ChangeIfContext(pInclude->zIf, pState);
			StringAppend(pState->pStr, "#include ", 0);
			StringAppend(pState->pStr, pInclude->zFile, 0);
			StringAppend(pState->pStr, "\n", 1);
		}
	}
}

/*
** Add to the string pStr a declaration for the object described
** in pDecl.
**
** If pDecl has already been declared in this file, detect that
** fact and abort early.  Do not duplicate a declaration.
**
** If the needFullDecl flag is false and this object has a forward
** declaration, then supply the forward declaration only.  A later
** call to CompleteForwardDeclarations() will finish the declaration
** for us.  But if needFullDecl is true, we must supply the full
** declaration now.  Some objects do not have a forward declaration.
** For those objects, we must print the full declaration now.
**
** Because it is illegal to duplicate a typedef in C, care is taken
** to insure that typedefs for the same identifier are only issued once.
*/
static void DeclareObject(Decl *pDecl, /* The thing to be declared */
			  GenState *pState, /* Current state of the code
					       generator */
			  int needFullDecl /* Must have the full declaration.  A
					    * forward declaration isn't enough
					    */
)
{
	Decl *p; /* The object to be declared */
	int flag;
	int isCpp; /* True if generating C++ */
	int doneTypedef = 0; /* True if a typedef has been done for this object
			      */

	/* printf("BEGIN %s of
	 * %s\n",needFullDecl?"FULL":"PROTOTYPE",pDecl->zName);*/
	/*
	** For any object that has a forward declaration, go ahead and do the
	** forward declaration first.
	*/
	isCpp = (pState->flags & DP_Cplusplus) != 0;
	for (p = pDecl; p; p = p->pSameName) {
		if (p->zFwd) {
			if (!DeclHasProperty(p, DP_Forward)) {
				DeclSetProperty(p, DP_Forward);
				if (strncmp(p->zFwd, "typedef", 7) == 0) {
					if (doneTypedef)
						continue;
					doneTypedef = 1;
				}
				ChangeIfContext(p->zIf, pState);
				StringAppend(pState->pStr,
					     isCpp ? p->zFwdCpp : p->zFwd, 0);
			}
		}
	}

	/*
	** Early out if everything is already suitably declared.
	**
	** This is a very important step because it prevents us from
	** executing the code the follows in a recursive call to this
	** function with the same value for pDecl.
	*/
	flag = needFullDecl ? DP_Declared | DP_Forward : DP_Forward;
	for (p = pDecl; p; p = p->pSameName) {
		if (!DeclHasProperty(p, flag))
			break;
	}
	if (p == 0) {
		return;
	}

	/*
	** Make sure we have all necessary #includes
	*/
	for (p = pDecl; p; p = p->pSameName) {
		AddIncludes(p->pInclude, pState);
	}

	/*
	** Go ahead an mark everything as being declared, to prevent an
	** infinite loop thru the ScanText() function.  At the same time,
	** we decide which objects need a full declaration and mark them
	** with the DP_Flag bit.  We are only able to use DP_Flag in this
	** way because we know we'll never execute this far into this
	** function on a recursive call with the same pDecl.  Hence, recursive
	** calls to this function (through ScanText()) can never change the
	** value of DP_Flag out from under us.
	*/
	for (p = pDecl; p; p = p->pSameName) {
		if (!DeclHasProperty(p, DP_Declared) &&
		    (p->zFwd == 0 || needFullDecl) && p->zDecl != 0) {
			DeclSetProperty(p, DP_Forward | DP_Declared | DP_Flag);
		} else {
			DeclClearProperty(p, DP_Flag);
		}
	}

	/*
	** Call ScanText() recursively (this routine is called from ScanText())
	** to include declarations required to come before these declarations.
	*/
	for (p = pDecl; p; p = p->pSameName) {
		if (DeclHasProperty(p, DP_Flag)) {
			if (p->zDecl[0] == '#') {
				ScanText(&p->zDecl[1], pState);
			} else {
				InsertExtraDecl(p);
				ScanText(p->zDecl, pState);
			}
		}
	}

	/*
	** Output the declarations.  Do this in two passes.  First
	** output everything that isn't a typedef.  Then go back and
	** get the typedefs by the same name.
	*/
	for (p = pDecl; p; p = p->pSameName) {
		if (DeclHasProperty(p, DP_Flag) &&
		    !DeclHasProperty(p, TY_Typedef)) {
			if (DeclHasAnyProperty(p, TY_Enumeration)) {
				if (doneTypedef)
					continue;
				doneTypedef = 1;
			}
			ChangeIfContext(p->zIf, pState);
			if (!isCpp && DeclHasAnyProperty(p, DP_ExternReqd)) {
				StringAppend(pState->pStr, "extern ", 0);
			} else if (isCpp &&
				   DeclHasProperty(p, DP_Cplusplus |
							      DP_ExternReqd)) {
				StringAppend(pState->pStr, "extern ", 0);
			} else if (isCpp &&
				   DeclHasAnyProperty(
					   p, DP_ExternCReqd | DP_ExternReqd)) {
				StringAppend(pState->pStr, "extern \"C\" ", 0);
			}
			InsertExtraDecl(p);
			StringAppend(pState->pStr, p->zDecl, 0);
			if (!isCpp && DeclHasProperty(p, DP_Cplusplus)) {
				fprintf(stderr,
					"%s: C code ought not reference the C++ object \"%s\"\n",
					pState->zFilename, p->zName);
				pState->nErr++;
			}
			DeclClearProperty(p, DP_Flag);
		}
	}
	for (p = pDecl; p && !doneTypedef; p = p->pSameName) {
		if (DeclHasProperty(p, DP_Flag)) {
			/* This has to be a typedef */
			doneTypedef = 1;
			ChangeIfContext(p->zIf, pState);
			InsertExtraDecl(p);
			StringAppend(pState->pStr, p->zDecl, 0);
		}
	}
}

/*
** This routine scans the input text given, and appends to the
** string in pState->pStr the text of any declarations that must
** occur before the text in zText.
**
** If an identifier in zText is immediately followed by '*', then
** only forward declarations are needed for that identifier.  If the
** identifier name is not followed immediately by '*', we must supply
** a full declaration.
*/
static void ScanText(const char *zText, /* The input text to be scanned */
		     GenState *pState /* Current state of the code generator */
)
{
	int nextValid = 0; /* True is sNext contains valid data */
	InStream sIn; /* The input text */
	Token sToken; /* The current token being examined */
	Token sNext; /* The next non-space token */

	/* printf("BEGIN SCAN TEXT on %s\n", zText); */

	sIn.z = zText;
	sIn.i = 0;
	sIn.nLine = 1;
	while (sIn.z[sIn.i] != 0) {
		if (nextValid) {
			sToken = sNext;
			nextValid = 0;
		} else {
			GetNonspaceToken(&sIn, &sToken);
		}
		if (sToken.eType == TT_Id) {
			int needFullDecl; /* True if we need to provide the full
					  *declaration,
					  ** not just the forward declaration */
			Decl *pDecl; /* The declaration having the name in
					sToken */

			/*
			** See if there is a declaration in the database with
			*the name given
			** by sToken.
			*/
			pDecl = FindDecl(sToken.zText, sToken.nText);
			if (pDecl == 0)
				continue;

			/*
			** If we get this far, we've found an identifier that
			*has a
			** declaration in the database.  Now see if we the full
			*declaration
			** or just a forward declaration.
			*/
			GetNonspaceToken(&sIn, &sNext);
			if (sNext.zText[0] == '*') {
				needFullDecl = 0;
			} else {
				needFullDecl = 1;
				nextValid = sNext.eType == TT_Id;
			}

			/*
			** Generate the needed declaration.
			*/
			DeclareObject(pDecl, pState, needFullDecl);
		} else if (sToken.eType == TT_Preprocessor) {
			sIn.i -= sToken.nText - 1;
		}
	}
	/* printf("END SCANTEXT\n"); */
}

/*
** Provide a full declaration to any object which so far has had only
** a forward declaration.
*/
static void CompleteForwardDeclarations(GenState *pState)
{
	Decl *pDecl;
	int progress;

	do {
		progress = 0;
		for (pDecl = pDeclFirst; pDecl; pDecl = pDecl->pNext) {
			if (DeclHasProperty(pDecl, DP_Forward) &&
			    !DeclHasProperty(pDecl, DP_Declared)) {
				DeclareObject(pDecl, pState, 1);
				progress = 1;
				assert(DeclHasProperty(pDecl, DP_Declared));
			}
		}
	} while (progress);
}

/*
** Generate an include file for the given source file.  Return the number
** of errors encountered.
**
** if nolocal_flag is true, then we do not generate declarations for
** objected marked DP_Local.
*/
static int MakeHeader(InFile *pFile, FILE *report, int nolocal_flag)
{
	int nErr = 0;
	GenState sState;
	String outStr;
	IdentTable includeTable;
	Ident *pId;
	char *zNewVersion;
	char *zOldVersion;

	if (pFile->zHdr == 0 || *pFile->zHdr == 0)
		return 0;
	sState.pStr = &outStr;
	StringInit(&outStr);
	StringAppend(&outStr, zTopLine, nTopLine);
	sState.pTable = &includeTable;
	memset(&includeTable, 0, sizeof(includeTable));
	sState.zIf = 0;
	sState.nErr = 0;
	sState.zFilename = pFile->zSrc;
	sState.flags = pFile->flags & DP_Cplusplus;
	ResetDeclFlags(nolocal_flag ? "no" : pFile->zSrc);
	for (pId = pFile->idTable.pList; pId; pId = pId->pNext) {
		Decl *pDecl = FindDecl(pId->zName, 0);
		if (pDecl) {
			DeclareObject(pDecl, &sState, 1);
		}
	}
	CompleteForwardDeclarations(&sState);
	ChangeIfContext(0, &sState);
	nErr += sState.nErr;
	zOldVersion = ReadFile(pFile->zHdr);
	zNewVersion = StringGet(&outStr);
	if (report)
		fprintf(report, "%s: ", pFile->zHdr);
	if (zOldVersion == 0) {
		if (report)
			fprintf(report, "updated\n");
		if (WriteFile(pFile->zHdr, zNewVersion)) {
			fprintf(stderr, "%s: Can't write to file\n",
				pFile->zHdr);
			nErr++;
		}
	} else if (strncmp(zOldVersion, zTopLine, nTopLine) != 0) {
		if (report)
			fprintf(report, "error!\n");
		fprintf(stderr,
			"%s: Can't overwrite this file because it wasn't previously\n"
			"%*s  generated by 'makeheaders'.\n",
			pFile->zHdr, (int)strlen(pFile->zHdr), "");
		nErr++;
	} else if (strcmp(zOldVersion, zNewVersion) != 0) {
		if (report)
			fprintf(report, "updated\n");
		if (WriteFile(pFile->zHdr, zNewVersion)) {
			fprintf(stderr, "%s: Can't write to file\n",
				pFile->zHdr);
			nErr++;
		}
	} else if (report) {
		fprintf(report, "unchanged\n");
	}
	SafeFree(zOldVersion);
	IdentTableReset(&includeTable);
	StringReset(&outStr);
	return nErr;
}

/*
** Generate a global header file -- a header file that contains all
** declarations.  If the forExport flag is true, then only those
** objects that are exported are included in the header file.
*/
static int MakeGlobalHeader(int forExport)
{
	GenState sState;
	String outStr;
	IdentTable includeTable;
	Decl *pDecl;

	sState.pStr = &outStr;
	StringInit(&outStr);
	/* StringAppend(&outStr,zTopLine,nTopLine); */
	sState.pTable = &includeTable;
	memset(&includeTable, 0, sizeof(includeTable));
	sState.zIf = 0;
	sState.nErr = 0;
	sState.zFilename = "(all)";
	sState.flags = 0;
	ResetDeclFlags(0);
	for (pDecl = pDeclFirst; pDecl; pDecl = pDecl->pNext) {
		if (forExport == 0 || DeclHasProperty(pDecl, DP_Export)) {
			DeclareObject(pDecl, &sState, 1);
		}
	}
	ChangeIfContext(0, &sState);
	printf("%s", StringGet(&outStr));
	IdentTableReset(&includeTable);
	StringReset(&outStr);
	return 0;
}

#ifdef DEBUG
/*
** Return the number of characters in the given string prior to the
** first newline.
*/
static int ClipTrailingNewline(char *z)
{
	int n = strlen(z);
	while (n > 0 && (z[n - 1] == '\n' || z[n - 1] == '\r')) {
		n--;
	}
	return n;
}

/*
** Dump the entire declaration list for debugging purposes
*/
static void DumpDeclList(void)
{
	Decl *pDecl;

	for (pDecl = pDeclFirst; pDecl; pDecl = pDecl->pNext) {
		printf("**** %s from file %s ****\n", pDecl->zName,
		       pDecl->zFile);
		if (pDecl->zIf) {
			printf("If: [%.*s]\n", ClipTrailingNewline(pDecl->zIf),
			       pDecl->zIf);
		}
		if (pDecl->zFwd) {
			printf("Decl: [%.*s]\n",
			       ClipTrailingNewline(pDecl->zFwd), pDecl->zFwd);
		}
		if (pDecl->zDecl) {
			InsertExtraDecl(pDecl);
			printf("Def: [%.*s]\n",
			       ClipTrailingNewline(pDecl->zDecl), pDecl->zDecl);
		}
		if (pDecl->flags) {
			static struct {
				int mask;
				char *desc;
			} flagSet[] = {
				{ TY_Class, "class" },
				{ TY_Enumeration, "enum" },
				{ TY_Structure, "struct" },
				{ TY_Union, "union" },
				{ TY_Variable, "variable" },
				{ TY_Subroutine, "function" },
				{ TY_Typedef, "typedef" },
				{ TY_Macro, "macro" },
				{ DP_Export, "export" },
				{ DP_Local, "local" },
				{ DP_Cplusplus, "C++" },
			};
			int i;
			printf("flags:");
			for (i = 0; i < sizeof(flagSet) / sizeof(flagSet[0]);
			     i++) {
				if (flagSet[i].mask & pDecl->flags) {
					printf(" %s", flagSet[i].desc);
				}
			}
			printf("\n");
		}
		if (pDecl->pInclude) {
			Include *p;
			printf("includes:");
			for (p = pDecl->pInclude; p; p = p->pNext) {
				printf(" %s", p->zFile);
			}
			printf("\n");
		}
	}
}
#endif

/*
** When the "-doc" command-line option is used, this routine is called
** to print all of the database information to standard output.
*/
static void DocumentationDump(void)
{
	Decl *pDecl;
	static struct {
		int mask;
		char flag;
	} flagSet[] = {
		{ TY_Class, 'c' },     { TY_Enumeration, 'e' },
		{ TY_Structure, 's' }, { TY_Union, 'u' },
		{ TY_Variable, 'v' },  { TY_Subroutine, 'f' },
		{ TY_Typedef, 't' },   { TY_Macro, 'm' },
		{ DP_Export, 'x' },    { DP_Local, 'l' },
		{ DP_Cplusplus, '+' },
	};

	for (pDecl = pDeclFirst; pDecl; pDecl = pDecl->pNext) {
		int i;
		int nLabel = 0;
		char *zDecl;
		char zLabel[50];
		for (i = 0; i < sizeof(flagSet) / sizeof(flagSet[0]); i++) {
			if (DeclHasProperty(pDecl, flagSet[i].mask)) {
				zLabel[nLabel++] = flagSet[i].flag;
			}
		}
		if (nLabel == 0)
			continue;
		zLabel[nLabel] = 0;
		InsertExtraDecl(pDecl);
		zDecl = pDecl->zDecl;
		if (zDecl == 0)
			zDecl = pDecl->zFwd;
		printf("%s %s %s %p %d %d %d %d %d\n", pDecl->zName, zLabel,
		       pDecl->zFile, pDecl->pComment,
		       pDecl->pComment ? pDecl->pComment->nText + 1 : 0,
		       pDecl->zIf ? (int)strlen(pDecl->zIf) + 1 : 0,
		       zDecl ? (int)strlen(zDecl) : 0,
		       pDecl->pComment ? pDecl->pComment->nLine : 0,
		       pDecl->tokenCode.nText ? pDecl->tokenCode.nText + 1 : 0);
		if (pDecl->pComment) {
			printf("%.*s\n", pDecl->pComment->nText,
			       pDecl->pComment->zText);
		}
		if (pDecl->zIf) {
			printf("%s\n", pDecl->zIf);
		}
		if (zDecl) {
			printf("%s", zDecl);
		}
		if (pDecl->tokenCode.nText) {
			printf("%.*s\n", pDecl->tokenCode.nText,
			       pDecl->tokenCode.zText);
		}
	}
}

/*
** Given the complete text of an input file, this routine prints a
** documentation record for the header comment at the beginning of the
** file (if the file has a header comment.)
*/
void PrintModuleRecord(const char *zFile, const char *zFilename)
{
	int i;
	static int addr = 5;
	while (isspace(*zFile)) {
		zFile++;
	}
	if (*zFile != '/' || zFile[1] != '*')
		return;
	for (i = 2; zFile[i] && (zFile[i - 1] != '/' || zFile[i - 2] != '*');
	     i++) {
	}
	if (zFile[i] == 0)
		return;
	printf("%s M %s %d %d 0 0 0 0\n%.*s\n", zFilename, zFilename, addr,
	       i + 1, i, zFile);
	addr += 4;
}

/*
** Given an input argument to the program, construct a new InFile
** object.
*/
static InFile *CreateInFile(char *zArg, int *pnErr)
{
	int nSrc;
	char *zSrc;
	InFile *pFile;
	int i;

	/*
	** Get the name of the input file to be scanned.  The input file is
	** everything before the first ':' or the whole file if no ':' is seen.
	**
	** Except, on windows, ignore any ':' that occurs as the second
	*character
	** since it might be part of the drive specifier.  So really, the ":'
	*has
	** to be the 3rd or later character in the name.  This precludes
	*1-character
	** file names, which really should not be a problem.
	*/
	zSrc = zArg;
	for (nSrc = 2; zSrc[nSrc] && zArg[nSrc] != ':'; nSrc++) {
	}
	pFile = SafeMalloc(sizeof(InFile));
	memset(pFile, 0, sizeof(InFile));
	pFile->zSrc = StrDup(zSrc, nSrc);

	/* Figure out if we are dealing with C or C++ code.  Assume any
	** file with ".c" or ".h" is C code and all else is C++.
	*/
	if (nSrc > 2 && zSrc[nSrc - 2] == '.' &&
	    (zSrc[nSrc - 1] == 'c' || zSrc[nSrc - 1] == 'h')) {
		pFile->flags &= ~DP_Cplusplus;
	} else {
		pFile->flags |= DP_Cplusplus;
	}

	/*
	** If a separate header file is specified, use it
	*/
	if (zSrc[nSrc] == ':') {
		int nHdr;
		char *zHdr;
		zHdr = &zSrc[nSrc + 1];
		for (nHdr = 0; zHdr[nHdr]; nHdr++) {
		}
		pFile->zHdr = StrDup(zHdr, nHdr);
	}

	/* Look for any 'c' or 'C' in the suffix of the file name and change
	** that character to 'h' or 'H' respectively.  If no 'c' or 'C' is
	*found,
	** then assume we are dealing with a header.
	*/
	else {
		int foundC = 0;
		pFile->zHdr = StrDup(zSrc, nSrc);
		for (i = nSrc - 1; i > 0 && pFile->zHdr[i] != '.'; i--) {
			if (pFile->zHdr[i] == 'c') {
				foundC = 1;
				pFile->zHdr[i] = 'h';
			} else if (pFile->zHdr[i] == 'C') {
				foundC = 1;
				pFile->zHdr[i] = 'H';
			}
		}
		if (!foundC) {
			SafeFree(pFile->zHdr);
			pFile->zHdr = 0;
		}
	}

	/*
	** If pFile->zSrc contains no 'c' or 'C' in its extension, it
	** must be a header file.   In that case, we need to set the
	** PS_Interface flag.
	*/
	pFile->flags |= PS_Interface;
	for (i = nSrc - 1; i > 0 && zSrc[i] != '.'; i--) {
		if (zSrc[i] == 'c' || zSrc[i] == 'C') {
			pFile->flags &= ~PS_Interface;
			break;
		}
	}

	/* Done!
	 */
	return pFile;
}

/* MS-Windows and MS-DOS both have the following serious OS bug:  the
** length of a command line is severely restricted.  But this program
** occasionally requires long command lines.  Hence the following
** work around.
**
** If the parameters "-f FILENAME" appear anywhere on the command line,
** then the named file is scanned for additional command line arguments.
** These arguments are substituted in place of the "FILENAME" argument
** in the original argument list.
**
** This first parameter to this routine is the index of the "-f"
** parameter in the argv[] array.  The argc and argv are passed by
** pointer so that they can be changed.
**
** Parsing of the parameters in the file is very simple.  Parameters
** can be separated by any amount of white-space (including newlines
** and carriage returns.)  There are now quoting characters of any
** kind.  The length of a token is limited to about 1000 characters.
*/
static void AddParameters(int index, int *pArgc, char ***pArgv)
{
	int argc = *pArgc; /* The original argc value */
	char **argv = *pArgv; /* The original argv value */
	int newArgc; /* Value for argc after inserting new arguments */
	char **zNew = 0; /* The new argv after this routine is done */
	char *zFile; /* Name of the input file */
	int nNew = 0; /* Number of new entries in the argv[] file */
	int nAlloc = 0; /* Space allocated for zNew[] */
	int i; /* Loop counter */
	int n; /* Number of characters in a new argument */
	int c; /* Next character of input */
	int startOfLine = 1; /* True if we are where '#' can start a comment */
	FILE *in; /* The input file */
	char zBuf[1000]; /* A single argument is accumulated here */

	if (index + 1 == argc)
		return;
	zFile = argv[index + 1];
	in = fopen(zFile, "r");
	if (in == 0) {
		fprintf(stderr, "Can't open input file \"%s\"\n", zFile);
		exit(1);
	}
	c = ' ';
	while (c != EOF) {
		while (c != EOF && isspace(c)) {
			if (c == '\n') {
				startOfLine = 1;
			}
			c = getc(in);
			if (startOfLine && c == '#') {
				while (c != EOF && c != '\n') {
					c = getc(in);
				}
			}
		}
		n = 0;
		while (c != EOF && !isspace(c)) {
			if (n < sizeof(zBuf) - 1) {
				zBuf[n++] = c;
			}
			startOfLine = 0;
			c = getc(in);
		}
		zBuf[n] = 0;
		if (n > 0) {
			nNew++;
			if (nNew + argc > nAlloc) {
				if (nAlloc == 0) {
					nAlloc = 100 + argc;
					zNew = malloc(sizeof(char *) * nAlloc);
				} else {
					nAlloc *= 2;
					zNew = realloc(zNew,
						       sizeof(char *) * nAlloc);
				}
			}
			if (zNew) {
				int j = nNew + index;
				zNew[j] = malloc(n + 1);
				if (zNew[j]) {
					strcpy(zNew[j], zBuf);
				}
			}
		}
	}
	fclose(in);
	newArgc = argc + nNew - 1;
	for (i = 0; i <= index; i++) {
		zNew[i] = argv[i];
	}
	for (i = nNew + index + 1; i < newArgc; i++) {
		zNew[i] = argv[i + 1 - nNew];
	}
	zNew[newArgc] = 0;
	*pArgc = newArgc;
	*pArgv = zNew;
}

#ifdef NOT_USED
/*
** Return the time that the given file was last modified.  If we can't
** locate the file (because, for example, it doesn't exist), then
** return 0.
*/
static unsigned int ModTime(const char *zFilename)
{
	unsigned int mTime = 0;
	struct stat sStat;
	if (stat(zFilename, &sStat) == 0) {
		mTime = sStat.st_mtime;
	}
	return mTime;
}
#endif

/*
** Print a usage comment for this program.
*/
static void Usage(const char *argv0, const char *argvN)
{
	fprintf(stderr, "%s: Illegal argument \"%s\"\n", argv0, argvN);
	fprintf(stderr,
		"Usage: %s [options] filename...\n"
		"Options:\n"
		"  -h          Generate a single .h to standard output.\n"
		"  -H          Like -h, but only output EXPORT declarations.\n"
		"  -v          (verbose) Write status information to the screen.\n"
		"  -doc        Generate no header files.  Instead, output information\n"
		"              that can be used by an automatic program documentation\n"
		"              and cross-reference generator.\n"
		"  -local      Generate prototypes for \"static\" functions and\n"
		"              procedures.\n"
		"  -f FILE     Read additional command-line arguments from the file named\n"
		"              \"FILE\".\n"
#ifdef DEBUG
		"  -! MASK     Set the debugging mask to the number \"MASK\".\n"
#endif
		"  --          Treat all subsequent comment-line parameters as filenames,\n"
		"              even if they begin with \"-\".\n",
		argv0);
}

/*
** The following text contains a few simple #defines that we want
** to be available to every file.
*/
static const char zInit[] = "#define INTERFACE 0\n"
			    "#define EXPORT_INTERFACE 0\n"
			    "#define LOCAL_INTERFACE 0\n"
			    "#define EXPORT\n"
			    "#define LOCAL static\n"
			    "#define PUBLIC\n"
			    "#define PRIVATE\n"
			    "#define PROTECTED\n";

#if TEST == 0
int main(int argc, char **argv)
{
	int i; /* Loop counter */
	int nErr = 0; /* Number of errors encountered */
	Token *pList; /* List of input tokens for one file */
	InFile *pFileList = 0; /* List of all input files */
	InFile *pTail = 0; /* Last file on the list */
	InFile *pFile; /* for looping over the file list */
	int h_flag = 0; /* True if -h is present.  Output unified header */
	int H_flag = 0; /* True if -H is present.  Output EXPORT header */
	int v_flag = 0; /* Verbose */
	int noMoreFlags; /* True if -- has been seen. */
	FILE *report; /* Send progress reports to this, if not NULL */

	noMoreFlags = 0;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] == '-' && !noMoreFlags) {
			switch (argv[i][1]) {
			case 'h':
				h_flag = 1;
				break;
			case 'H':
				H_flag = 1;
				break;
			case 'v':
				v_flag = 1;
				break;
			case 'd':
				doc_flag = 1;
				proto_static = 1;
				break;
			case 'l':
				proto_static = 1;
				break;
			case 'f':
				AddParameters(i, &argc, &argv);
				break;
			case '-':
				noMoreFlags = 1;
				break;
#ifdef DEBUG
			case '!':
				i++;
				debugMask = strtol(argv[i], 0, 0);
				break;
#endif
			default:
				Usage(argv[0], argv[i]);
				return 1;
			}
		} else {
			pFile = CreateInFile(argv[i], &nErr);
			if (pFile) {
				if (pFileList) {
					pTail->pNext = pFile;
					pTail = pFile;
				} else {
					pFileList = pTail = pFile;
				}
			}
		}
	}
	if (h_flag && H_flag) {
		h_flag = 0;
	}
	if (v_flag) {
		report = (h_flag || H_flag) ? stderr : stdout;
	} else {
		report = 0;
	}
	if (nErr > 0) {
		return nErr;
	}
	for (pFile = pFileList; pFile; pFile = pFile->pNext) {
		char *zFile;

		zFilename = pFile->zSrc;
		if (zFilename == 0)
			continue;
		zFile = ReadFile(zFilename);
		if (zFile == 0) {
			fprintf(stderr, "Can't read input file \"%s\"\n",
				zFilename);
			nErr++;
			continue;
		}
		if (strncmp(zFile, zTopLine, nTopLine) == 0) {
			pFile->zSrc = 0;
		} else {
			if (report)
				fprintf(report, "Reading %s...\n", zFilename);
			pList = TokenizeFile(zFile, &pFile->idTable);
			if (pList) {
				nErr += ParseFile(pList, pFile->flags);
				FreeTokenList(pList);
			} else if (zFile[0] == 0) {
				fprintf(stderr, "Input file \"%s\" is empty.\n",
					zFilename);
				nErr++;
			} else {
				fprintf(stderr,
					"Errors while processing \"%s\"\n",
					zFilename);
				nErr++;
			}
		}
		if (!doc_flag)
			SafeFree(zFile);
		if (doc_flag)
			PrintModuleRecord(zFile, zFilename);
	}
	if (nErr > 0) {
		return nErr;
	}
#ifdef DEBUG
	if (debugMask & DECL_DUMP) {
		DumpDeclList();
		return nErr;
	}
#endif
	if (doc_flag) {
		DocumentationDump();
		return nErr;
	}
	zFilename = "--internal--";
	pList = TokenizeFile(zInit, 0);
	if (pList == 0) {
		return nErr + 1;
	}
	ParseFile(pList, PS_Interface);
	FreeTokenList(pList);
	if (h_flag || H_flag) {
		nErr += MakeGlobalHeader(H_flag);
	} else {
		for (pFile = pFileList; pFile; pFile = pFile->pNext) {
			if (pFile->zSrc == 0)
				continue;
			nErr += MakeHeader(pFile, report, 0);
		}
	}
	return nErr;
}
#endif
