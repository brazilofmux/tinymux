// Definitions for user-defined attributes.
//
// $Id: vattr.h,v 1.2 2000-04-15 17:25:46 sdennis Exp $
//
typedef struct user_attribute VATTR;
struct user_attribute
{
    char *name;    // Name of user attribute
    int  number;   // Assigned attribute number
    int  flags;    // Attribute flags
};

extern void  vattr_init(void);
extern VATTR *vattr_rename_LEN(char *, int, char *, int);
extern VATTR *vattr_find_LEN(char *, int);
extern VATTR *vattr_alloc_LEN(char *, int, int);
extern VATTR *vattr_define_LEN(char *, int, int, int);
extern void  vattr_delete_LEN(char *, int);
extern VATTR *attr_rename(char *, char *);
extern VATTR *vattr_first(void);
extern VATTR *vattr_next(VATTR *);
extern void  list_vhashstats(dbref);
