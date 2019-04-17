#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "encoder.h"
#include "decoder.h"


static struct PyModuleDef _cboarmodule = {
    PyModuleDef_HEAD_INIT,
    .m_name = "_cboar",
    .m_doc = NULL,
    .m_size = -1, // XXX Change to 0?
};


PyMODINIT_FUNC
PyInit__cboar(void)
{
    PyObject *module;
    if (PyType_Ready(&EncoderType) < 0)
        return NULL;
    if (PyType_Ready(&DecoderType) < 0)
        return NULL;

    module = PyModule_Create(&_cboarmodule);
    if (!module)
        return NULL;

    Py_INCREF(&EncoderType);
    if (PyModule_AddObject(module, "Encoder", (PyObject *) &EncoderType) == -1) {
        Py_DECREF(module);
        return NULL;
    }

    Py_INCREF(&DecoderType);
    if (PyModule_AddObject(module, "Decoder", (PyObject *) &DecoderType) == -1) {
        Py_DECREF(module);
        return NULL;
    }

    return module;
}
