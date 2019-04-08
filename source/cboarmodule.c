#include <Python.h>

static struct PyModuleDef cboarmodule;


static PyObject *
cboar_hello(PyObject *self, PyObject *args)
{
    const char *name;
    int len;

    if (!PyArg_ParseTuple(args, "s", &name))
        return NULL;
    len = printf("Hello, %s!\n", name);
    return PyLong_FromLong(len);
}


PyMODINIT_FUNC
PyInit_cboar(void)
{
    PyObject *m;

    m = PyModule_Create(&cboarmodule);
    if (m == NULL) return NULL;

    return m;
}

static PyMethodDef cboarmethods[] = {
    {"hello", cboar_hello, METH_VARARGS, "Say hello to something."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef cboarmodule = {
    PyModuleDef_HEAD_INIT,
    "cboar",           // module name
    NULL,              // module doc-string
    -1,                // size of per-interpreter state
    cboarmethods       // module method table
};
