#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "tags.h"

static int Tag_set_tag(TagObject *, PyObject *, void *);
static int Tag_set_value(TagObject *, PyObject *, void *);


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
    PyObject *tag = NULL, *value = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwargs, "|OO", keywords, &tag, &value))
        return -1;

    if (tag && Tag_set_tag(self, tag, NULL) == -1)
        return -1;
    if (value && Tag_set_value(self, value, NULL) == -1)
        return -1;

    return 0;
}


// Property accessors ////////////////////////////////////////////////////////

// Tag._get_tag(self)
static PyObject *
Tag_get_tag(TagObject *self, void *closure)
{
    return PyLong_FromUnsignedLongLong(self->tag);
}


// Tag._set_tag(self, value)
static int
Tag_set_tag(TagObject *self, PyObject *value, void *closure)
{
    uint64_t tag;

    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete tag attribute");
        return -1;
    }
    if (!PyLong_CheckExact(value)) {
        PyErr_Format(PyExc_ValueError,
                "invalid tag value %R (expected int)", value);
        return -1;
    }
    tag = PyLong_AsUnsignedLongLong(value);
    if (PyErr_Occurred())
        return -1;

    self->tag = tag;
    return 0;
}


// Tag._get_value(self)
static PyObject *
Tag_get_value(TagObject *self, void *closure)
{
    Py_INCREF(self->value);
    return self->value;
}


// Tag._set_value(self, value)
static int
Tag_set_value(TagObject *self, PyObject *value, void *closure)
{
    PyObject *tmp;

    if (!value) {
        PyErr_SetString(PyExc_TypeError, "cannot delete value attribute");
        return -1;
    }

    tmp = self->value;
    Py_INCREF(value);
    self->value = value;
    Py_DECREF(tmp);
    return 0;
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
    if (!Tag_CheckExact(tag)) {
        PyErr_BadInternalCall();
        return -1;
    }
    return Tag_set_value((TagObject *)tag, value, NULL);
}


// Tag class definition //////////////////////////////////////////////////////

static PyGetSetDef Tag_getsetters[] = {
    {"tag", (getter) Tag_get_tag, (setter) Tag_set_tag,
        "integer tag value", NULL},
    {"value", (getter) Tag_get_value, (setter) Tag_set_value,
        "the decoded value", NULL},
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
    .tp_getset = Tag_getsetters,
};
