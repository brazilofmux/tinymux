// vattr.h -- Definitions for user-defined attributes.
//
// $Id$
//

extern ATTR *vattr_rename_LEN(char *pOldName, size_t nOldName, char *pNewName, size_t nNewName);
extern ATTR *vattr_find_LEN(const char *pAttrName, size_t nAttrName);
extern ATTR *vattr_alloc_LEN(char *pAttrName, size_t nAttrName, int flags);
extern ATTR *vattr_define_LEN(char *pAttrName, size_t nAttrName, int number, int flags);
extern void  vattr_delete_LEN(char *pName, size_t nName);
extern ATTR *vattr_first(void);
extern ATTR *vattr_next(ATTR *);
extern void  list_vhashstats(dbref);
