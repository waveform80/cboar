#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "structmember.h"
#include "tags.h"


// Constructors and destructors //////////////////////////////////////////////

// Tag.__del__(self)
static void
Tag_dealloc(TagObject *self)
{
    Py_XDECREF(self->value);
    Py_TYPE(self)->tp_free((PyObject *) self);
}


// Tag.__new__(cls, *args, **kwargs)
static PyObject *
Tag_new(PyTypeObject *type, PyObject *args, PyObject *kwargs)
{
    TagObject *self;

    self = (TagObject *) type->tp_alloc(type, 0);
    if (self) {
        self->tag = 0;
        Py_INCREF(Py_None);
        self->value = Py_None;
    }
    return (PyObject *) self;
}


// Tag.__init__(self, tag=None, value=None)
static int
Tag_init(TagObject *self, PyObject *args, PyObject *kwargs)
{
    static char *keywords[] = {"tag", "value", NULL};
    PyObject *tmp, *value = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|KO", keywords,
                &self->tag, &value))
        return -1;

    if (value) {
        tmp = self->value;
        Py_INCREF(value);
        self->value = value;
        Py_XDECREF(tmp);
    }
    return 0;
}


// Special methods ///////////////////////////////////////////////////////////

static PyObject *
Tag_repr(TagObject *self)
{
    PyObject *ret = NULL;

    if (Py_ReprEnter((PyObject *)self))
        ret = PyUnicode_FromString("CBORTag(...)");
    else
        ret = PyUnicode_FromFormat("CBORTag(%llu, %R)", self->tag, self->value);
    Py_ReprLeave((PyObject *)self);
    return ret;
}


static PyObject *
Tag_richcompare(PyObject *aobj, PyObject *bobj, int op)
{
    PyObject *ret = NULL;
    TagObject *a, *b;

    if (!(Tag_CheckExact(aobj) && Tag_CheckExact(bobj))) {
        Py_RETURN_NOTIMPLEMENTED;
    } else {
        a = (TagObject *)aobj;
        b = (TagObject *)bobj;

        if (a->tag == b->tag) {
            ret = PyObject_RichCompare(a->value, b->value, op);
        } else {
            switch (op) {
                case Py_EQ: ret = Py_False; break;
                case Py_NE: ret = Py_True;  break;
                case Py_LT: ret = a->tag <  b->tag ? Py_True : Py_False; break;
                case Py_LE: ret = a->tag <= b->tag ? Py_True : Py_False; break;
                case Py_GE: ret = a->tag >= b->tag ? Py_True : Py_False; break;
                case Py_GT: ret = a->tag >  b->tag ? Py_True : Py_False; break;
                default: assert(0);
            }
            Py_INCREF(ret);
        }
    }
    return ret;
}


// C API /////////////////////////////////////////////////////////////////////

PyObject *
Tag_New(uint64_t tag)
{
    TagObject *ret = NULL;

    ret = PyObject_New(TagObject, &TagType);
    if (ret) {
        ret->tag = tag;
        Py_INCREF(Py_None);
        ret->value = Py_None;
    }
    return (PyObject *)ret;
}

int
Tag_SetValue(PyObject *tag, PyObject *value)
{
    PyObject *tmp;
    TagObject *self;

    if (!Tag_CheckExact(tag))
        return -1;
    if (!value)
        return -1;

    self = (TagObject*)tag;
    tmp = self->value;
    Py_INCREF(value);
    self->value = value;
    Py_XDECREF(tmp);
    return 0;
}


// Tag class definition //////////////////////////////////////////////////////

static PyMemberDef Tag_members[] = {
    {"tag", T_ULONGLONG, offsetof(TagObject, tag), 0,
        "the semantic tag associated with the value"},
    {"value", T_OBJECT_EX, offsetof(TagObject, value), 0,
        "the tagged value"},
    {NULL}
};

PyTypeObject TagType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "_cboar.Tag",
    .tp_doc = "CBOAR tag objects",
    .tp_basicsize = sizeof(TagObject),
    .tp_itemsize = 0,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_new = Tag_new,
    .tp_init = (initproc) Tag_init,
    .tp_dealloc = (destructor) Tag_dealloc,
    .tp_members = Tag_members,
    .tp_repr = (reprfunc) Tag_repr,
    .tp_richcompare = Tag_richcompare,
};
