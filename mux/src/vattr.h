/*! \file vattr.h
 * \brief Definitions for user-defined attributes.
 *
 */

extern ATTR *vattr_rename_LEN(UTF8 *pOldName, size_t nOldName, UTF8 *pNewName, size_t nNewName);
extern ATTR *vattr_find_LEN(const UTF8 *pAttrName, size_t nAttrName);
extern ATTR *vattr_alloc_LEN(const UTF8 *pAttrName, size_t nAttrName, int flags);
extern ATTR *vattr_define_LEN(const UTF8 *pAttrName, size_t nAttrName, int number, int flags);
extern void  vattr_delete_LEN(UTF8 *pName, size_t nName);
extern ATTR *vattr_first(void);
extern ATTR *vattr_next(ATTR *);
extern void  list_vhashstats(dbref);
