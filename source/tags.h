#define PY_SSIZE_T_CLEAN
#include <Python.h>

typedef struct {
    PyObject_HEAD
    uint64_t tag;
    PyObject *value;
} TagObject;

PyTypeObject TagType;

PyObject * Tag_New(uint64_t);
int Tag_SetValue(PyObject *, PyObject *);

#define Tag_CheckExact(op) (Py_TYPE(op) == &TagType)
