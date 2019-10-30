/*
resultproxy.c
Copyright (C) 2010-2019 the SQLAlchemy authors and contributors <see AUTHORS file>
Copyright (C) 2010-2011 Gaetan de Menten gdementen@gmail.com

This module is part of SQLAlchemy and is released under
the MIT License: http://www.opensource.org/licenses/mit-license.php
*/

#include <Python.h>

#define MODULE_NAME "cresultproxy"
#define MODULE_DOC "Module containing C versions of core ResultProxy classes."

#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
typedef Py_ssize_t (*lenfunc)(PyObject *);
#define PyInt_FromSsize_t(x) PyInt_FromLong(x)
typedef intargfunc ssizeargfunc;
#endif


/***********
 * Structs *
 ***********/

typedef struct {
    PyObject_HEAD
    PyObject *parent;
    PyObject *row;
    PyObject *keymap;
} BaseRow;

/****************
 * BaseRow *
 ****************/

static PyObject *
safe_rowproxy_reconstructor(PyObject *self, PyObject *args)
{
    PyObject *cls, *state, *tmp;
    BaseRow *obj;

    if (!PyArg_ParseTuple(args, "OO", &cls, &state))
        return NULL;

    obj = (BaseRow *)PyObject_CallMethod(cls, "__new__", "O", cls);
    if (obj == NULL)
        return NULL;

    tmp = PyObject_CallMethod((PyObject *)obj, "__setstate__", "O", state);
    if (tmp == NULL) {
        Py_DECREF(obj);
        return NULL;
    }
    Py_DECREF(tmp);

    if (obj->parent == NULL || obj->row == NULL ||
        obj->keymap == NULL) {
        PyErr_SetString(PyExc_RuntimeError,
            "__setstate__ for BaseRow subclasses must set values "
            "for parent, row, and keymap");
        Py_DECREF(obj);
        return NULL;
    }

    return (PyObject *)obj;
}

static int
BaseRow_init(BaseRow *self, PyObject *args, PyObject *kwds)
{
    PyObject *parent, *keymap, *row, *processors;
    Py_ssize_t num_values, num_processors;
    PyObject **valueptr, **funcptr, **resultptr;
    PyObject *func, *result, *processed_value, *values_fastseq;

    if (!PyArg_UnpackTuple(args, "BaseRow", 4, 4,
                           &parent, &processors, &keymap, &row))
        return -1;

    Py_INCREF(parent);
    self->parent = parent;

    values_fastseq = PySequence_Fast(row, "row must be a sequence");
    if (values_fastseq == NULL)
        return -1;

    num_values = PySequence_Length(values_fastseq);
    num_processors = PyList_Size(processors);
    if (num_values != num_processors) {
        PyErr_Format(PyExc_RuntimeError,
            "number of values in row (%d) differ from number of column "
            "processors (%d)",
            (int)num_values, (int)num_processors);
        return -1;
    }

    result = PyTuple_New(num_values);
    if (result == NULL)
        return -1;

    valueptr = PySequence_Fast_ITEMS(values_fastseq);
    funcptr = PySequence_Fast_ITEMS(processors);
    resultptr = PySequence_Fast_ITEMS(result);
    while (--num_values >= 0) {
        func = *funcptr;
        if (func != Py_None) {
            processed_value = PyObject_CallFunctionObjArgs(
                func, *valueptr, NULL);
            if (processed_value == NULL) {
                Py_DECREF(values_fastseq);
                Py_DECREF(result);
                return -1;
            }
            *resultptr = processed_value;
        } else {
            Py_INCREF(*valueptr);
            *resultptr = *valueptr;
        }
        valueptr++;
        funcptr++;
        resultptr++;
    }

    Py_DECREF(values_fastseq);

    self->row = result;

    if (!PyDict_CheckExact(keymap)) {
        PyErr_SetString(PyExc_TypeError, "keymap must be a dict");
        return -1;
    }
    Py_INCREF(keymap);
    self->keymap = keymap;

    return 0;
}

/* We need the reduce method because otherwise the default implementation
 * does very weird stuff for pickle protocol 0 and 1. It calls
 * BaseRow.__new__(Row_instance) upon *pickling*.
 */
static PyObject *
BaseRow_reduce(PyObject *self)
{
    PyObject *method, *state;
    PyObject *module, *reconstructor, *cls;

    method = PyObject_GetAttrString(self, "__getstate__");
    if (method == NULL)
        return NULL;

    state = PyObject_CallObject(method, NULL);
    Py_DECREF(method);
    if (state == NULL)
        return NULL;

    module = PyImport_ImportModule("sqlalchemy.engine.result");
    if (module == NULL)
        return NULL;

    reconstructor = PyObject_GetAttrString(module, "rowproxy_reconstructor");
    Py_DECREF(module);
    if (reconstructor == NULL) {
        Py_DECREF(state);
        return NULL;
    }

    cls = PyObject_GetAttrString(self, "__class__");
    if (cls == NULL) {
        Py_DECREF(reconstructor);
        Py_DECREF(state);
        return NULL;
    }

    return Py_BuildValue("(N(NN))", reconstructor, cls, state);
}

static void
BaseRow_dealloc(BaseRow *self)
{
    Py_XDECREF(self->parent);
    Py_XDECREF(self->row);
    Py_XDECREF(self->keymap);
#if PY_MAJOR_VERSION >= 3
    Py_TYPE(self)->tp_free((PyObject *)self);
#else
    self->ob_type->tp_free((PyObject *)self);
#endif
}

static PyObject *
BaseRow_valuescollection(PyObject *values, int astuple)
{
    PyObject *result;

    if (astuple) {
        result = PySequence_Tuple(values);
    } else {
        result = PySequence_List(values);
    }
    if (result == NULL)
        return NULL;

    return result;
}

static PyListObject *
BaseRow_values_impl(BaseRow *self)
{
    return (PyListObject *)BaseRow_valuescollection(self->row, 0);
}

static Py_hash_t
BaseRow_hash(BaseRow *self)
{
    return PyObject_Hash(self->row);
}

static PyObject *
BaseRow_iter(BaseRow *self)
{
    PyObject *values, *result;

    values = BaseRow_valuescollection(self->row, 1);
    if (values == NULL)
        return NULL;

    result = PyObject_GetIter(values);
    Py_DECREF(values);
    if (result == NULL)
        return NULL;

    return result;
}

static Py_ssize_t
BaseRow_length(BaseRow *self)
{
    return PySequence_Length(self->row);
}

static PyObject *
BaseRow_getitem(BaseRow *self, Py_ssize_t i)
{
    PyObject *value;
    PyObject *row;

    row = self->row;

    // row is a Tuple
    value = PyTuple_GetItem(row, i);

    if (value == NULL)
        return NULL;

    Py_INCREF(value);

    return value;
}

static PyObject *
BaseRow_getitem_by_object(BaseRow *self, PyObject *key)
{
    PyObject *record, *indexobject;
    PyObject *exc_module, *exception, *cstr_obj;
#if PY_MAJOR_VERSION >= 3
    PyObject *bytes;
#endif
    char *cstr_key;
    long index;
    int key_fallback = 0;

    // if record is non null, it's a borrowed reference
    record = PyDict_GetItem((PyObject *)self->keymap, key);

    if (record == NULL) {
        record = PyObject_CallMethod(self->parent, "_key_fallback",
                                     "O", key);
        if (record == NULL)
            return NULL;
        key_fallback = 1;  // boolean to indicate record is a new reference
    }

    indexobject = PyTuple_GetItem(record, 0);
    if (indexobject == NULL)
        return NULL;

    if (key_fallback) {
        Py_DECREF(record);
    }

    if (indexobject == Py_None) {
        exc_module = PyImport_ImportModule("sqlalchemy.exc");
        if (exc_module == NULL)
            return NULL;

        exception = PyObject_GetAttrString(exc_module,
                                           "InvalidRequestError");
        Py_DECREF(exc_module);
        if (exception == NULL)
            return NULL;

        cstr_obj = PyTuple_GetItem(record, 2);
        if (cstr_obj == NULL)
            return NULL;

        cstr_obj = PyObject_Str(cstr_obj);
        if (cstr_obj == NULL)
            return NULL;

/*
       FIXME: raise encoding error exception (in both versions below)
       if the key contains non-ascii chars, instead of an
       InvalidRequestError without any message like in the
       python version.
*/


#if PY_MAJOR_VERSION >= 3
        bytes = PyUnicode_AsASCIIString(cstr_obj);
        if (bytes == NULL)
            return NULL;
        cstr_key = PyBytes_AS_STRING(bytes);
#else
        cstr_key = PyString_AsString(cstr_obj);
#endif
        if (cstr_key == NULL) {
            Py_DECREF(cstr_obj);
            return NULL;
        }
        Py_DECREF(cstr_obj);

        PyErr_Format(exception,
                "Ambiguous column name '%.200s' in "
                "result set column descriptions", cstr_key);
        return NULL;
    }

#if PY_MAJOR_VERSION >= 3
    index = PyLong_AsLong(indexobject);
#else
    index = PyInt_AsLong(indexobject);
#endif
    if ((index == -1) && PyErr_Occurred())
        /* -1 can be either the actual value, or an error flag. */
        return NULL;

    return BaseRow_getitem(self, index);

}

static PyObject *
BaseRow_subscript_impl(BaseRow *self, PyObject *key, int asmapping)
{
    PyObject *values;
    PyObject *result;
    long index;

#if PY_MAJOR_VERSION < 3
    if (PyInt_CheckExact(key)) {
        index = PyInt_AS_LONG(key);
        if (index < 0)
            index += BaseRow_length(self);
        return BaseRow_getitem(self, index);
    } else
#endif

    if (PyLong_CheckExact(key)) {
        index = PyLong_AsLong(key);
        if ((index == -1) && PyErr_Occurred())
            /* -1 can be either the actual value, or an error flag. */
            return NULL;
        if (index < 0)
            index += BaseRow_length(self);
        return BaseRow_getitem(self, index);
    } else if (PySlice_Check(key)) {
        values = PyObject_GetItem(self->row, key);
        if (values == NULL)
            return NULL;

        result = BaseRow_valuescollection(values, 1);
        Py_DECREF(values);
        return result;
    } else {
        /*
         // if we want to warn for non-integer access by getitem,
         // that would happen here.
         if (!asmapping) {
            tmp = PyObject_CallMethod(self->parent, "_warn_for_nonint", "");
            if (tmp == NULL) {
                return NULL;
            }
            Py_DECREF(tmp);
        }*/
        return BaseRow_getitem_by_object(self, key);
    }
}

static PyObject *
BaseRow_subscript(BaseRow *self, PyObject *key)
{
    return BaseRow_subscript_impl(self, key, 0);
}

static PyObject *
BaseRow_subscript_mapping(BaseRow *self, PyObject *key)
{
    return BaseRow_subscript_impl(self, key, 1);
}


static PyObject *
BaseRow_getattro(BaseRow *self, PyObject *name)
{
    PyObject *tmp;
#if PY_MAJOR_VERSION >= 3
    PyObject *err_bytes;
#endif

    if (!(tmp = PyObject_GenericGetAttr((PyObject *)self, name))) {
        if (!PyErr_ExceptionMatches(PyExc_AttributeError))
            return NULL;
        PyErr_Clear();
    }
    else
        return tmp;

    tmp = BaseRow_subscript_mapping(self, name);
    if (tmp == NULL && PyErr_ExceptionMatches(PyExc_KeyError)) {

#if PY_MAJOR_VERSION >= 3
        err_bytes = PyUnicode_AsASCIIString(name);
        if (err_bytes == NULL)
            return NULL;
        PyErr_Format(
                PyExc_AttributeError,
                "Could not locate column in row for column '%.200s'",
                PyBytes_AS_STRING(err_bytes)
            );
#else
        PyErr_Format(
                PyExc_AttributeError,
                "Could not locate column in row for column '%.200s'",
                PyString_AsString(name)
            );
#endif
        return NULL;
    }
    return tmp;
}

/***********************
 * getters and setters *
 ***********************/

static PyObject *
BaseRow_getparent(BaseRow *self, void *closure)
{
    Py_INCREF(self->parent);
    return self->parent;
}

static int
BaseRow_setparent(BaseRow *self, PyObject *value, void *closure)
{
    PyObject *module, *cls;

    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "Cannot delete the 'parent' attribute");
        return -1;
    }

    module = PyImport_ImportModule("sqlalchemy.engine.result");
    if (module == NULL)
        return -1;

    cls = PyObject_GetAttrString(module, "ResultMetaData");
    Py_DECREF(module);
    if (cls == NULL)
        return -1;

    if (PyObject_IsInstance(value, cls) != 1) {
        PyErr_SetString(PyExc_TypeError,
                        "The 'parent' attribute value must be an instance of "
                        "ResultMetaData");
        return -1;
    }
    Py_DECREF(cls);
    Py_XDECREF(self->parent);
    Py_INCREF(value);
    self->parent = value;

    return 0;
}

static PyObject *
BaseRow_getrow(BaseRow *self, void *closure)
{
    Py_INCREF(self->row);
    return self->row;
}

static int
BaseRow_setrow(BaseRow *self, PyObject *value, void *closure)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "Cannot delete the 'row' attribute");
        return -1;
    }

    if (!PySequence_Check(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "The 'row' attribute value must be a sequence");
        return -1;
    }

    Py_XDECREF(self->row);
    Py_INCREF(value);
    self->row = value;

    return 0;
}



static PyObject *
BaseRow_getkeymap(BaseRow *self, void *closure)
{
    Py_INCREF(self->keymap);
    return self->keymap;
}

static int
BaseRow_setkeymap(BaseRow *self, PyObject *value, void *closure)
{
    if (value == NULL) {
        PyErr_SetString(PyExc_TypeError,
                        "Cannot delete the 'keymap' attribute");
        return -1;
    }

    if (!PyDict_CheckExact(value)) {
        PyErr_SetString(PyExc_TypeError,
                        "The 'keymap' attribute value must be a dict");
        return -1;
    }

    Py_XDECREF(self->keymap);
    Py_INCREF(value);
    self->keymap = value;

    return 0;
}

static PyGetSetDef BaseRow_getseters[] = {
    {"_parent",
     (getter)BaseRow_getparent, (setter)BaseRow_setparent,
     "ResultMetaData",
     NULL},
    {"_data",
     (getter)BaseRow_getrow, (setter)BaseRow_setrow,
     "processed data list",
     NULL},
    {"_keymap",
     (getter)BaseRow_getkeymap, (setter)BaseRow_setkeymap,
     "Key to (obj, index) dict",
     NULL},
    {NULL}
};

static PyMethodDef BaseRow_methods[] = {
    {"_values_impl", (PyCFunction)BaseRow_values_impl, METH_NOARGS,
     "Return the values represented by this BaseRow as a list."},
    {"__reduce__",  (PyCFunction)BaseRow_reduce, METH_NOARGS,
     "Pickle support method."},
    {"_get_by_key_impl", (PyCFunction)BaseRow_subscript, METH_O,
    "implement mapping-like getitem as well as sequence getitem"},
    {"_get_by_key_impl_mapping", (PyCFunction)BaseRow_subscript_mapping, METH_O,
    "implement mapping-like getitem as well as sequence getitem"},
    {NULL}  /* Sentinel */
};

static PySequenceMethods BaseRow_as_sequence = {
    (lenfunc)BaseRow_length,   /* sq_length */
    0,                              /* sq_concat */
    0,                              /* sq_repeat */
    (ssizeargfunc)BaseRow_getitem,  /* sq_item */
    0,                              /* sq_slice */
    0,                              /* sq_ass_item */
    0,                              /* sq_ass_slice */
    0,                              /* sq_contains */
    0,                              /* sq_inplace_concat */
    0,                              /* sq_inplace_repeat */
};

static PyMappingMethods BaseRow_as_mapping = {
    (lenfunc)BaseRow_length,       /* mp_length */
    (binaryfunc)BaseRow_subscript_mapping, /* mp_subscript */
    0                                   /* mp_ass_subscript */
};

static PyTypeObject BaseRowType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "sqlalchemy.cresultproxy.BaseRow",          /* tp_name */
    sizeof(BaseRow),               /* tp_basicsize */
    0,                                  /* tp_itemsize */
    (destructor)BaseRow_dealloc,   /* tp_dealloc */
    0,                                  /* tp_print */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_compare */
    0,                                  /* tp_repr */
    0,                                  /* tp_as_number */
    &BaseRow_as_sequence,          /* tp_as_sequence */
    &BaseRow_as_mapping,           /* tp_as_mapping */
    (hashfunc)BaseRow_hash,             /* tp_hash */
    0,                                  /* tp_call */
    0,                                  /* tp_str */
    (getattrofunc)BaseRow_getattro,/* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE,               /* tp_flags */
    "BaseRow is a abstract base class for Row",   /* tp_doc */
    0,                                  /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    (getiterfunc)BaseRow_iter,     /* tp_iter */
    0,                                  /* tp_iternext */
    BaseRow_methods,               /* tp_methods */
    0,                                  /* tp_members */
    BaseRow_getseters,             /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    (initproc)BaseRow_init,        /* tp_init */
    0,                                  /* tp_alloc */
    0                                   /* tp_new */
};



/* _tuplegetter function ************************************************/
/*
retrieves segments of a row as tuples.

mostly like operator.itemgetter but calls a fixed method instead,
returns tuple every time.

*/

typedef struct {
    PyObject_HEAD
    Py_ssize_t nitems;
    PyObject *item;
} tuplegetterobject;

static PyTypeObject tuplegetter_type;

static PyObject *
tuplegetter_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    tuplegetterobject *tg;
    PyObject *item;
    Py_ssize_t nitems;

    if (!_PyArg_NoKeywords("tuplegetter", kwds))
        return NULL;

    nitems = PyTuple_GET_SIZE(args);
    item = args;

    tg = PyObject_GC_New(tuplegetterobject, &tuplegetter_type);
    if (tg == NULL)
        return NULL;

    Py_INCREF(item);
    tg->item = item;
    tg->nitems = nitems;
    PyObject_GC_Track(tg);
    return (PyObject *)tg;
}

static void
tuplegetter_dealloc(tuplegetterobject *tg)
{
    PyObject_GC_UnTrack(tg);
    Py_XDECREF(tg->item);
    PyObject_GC_Del(tg);
}

static int
tuplegetter_traverse(tuplegetterobject *tg, visitproc visit, void *arg)
{
    Py_VISIT(tg->item);
    return 0;
}

static PyObject *
tuplegetter_call(tuplegetterobject *tg, PyObject *args, PyObject *kw)
{
    PyObject *row, *result;
    Py_ssize_t i, nitems=tg->nitems;

    assert(PyTuple_CheckExact(args));

    // this is normally a BaseRow subclass but we are not doing
    // strict checking at the moment
    row = PyTuple_GET_ITEM(args, 0);

    assert(PyTuple_Check(tg->item));
    assert(PyTuple_GET_SIZE(tg->item) == nitems);

    result = PyTuple_New(nitems);
    if (result == NULL)
        return NULL;

    for (i=0 ; i < nitems ; i++) {
        PyObject *item, *val;
        item = PyTuple_GET_ITEM(tg->item, i);

        val = PyObject_CallMethod(row, "_get_by_key_impl_mapping", "O", item);

        // generic itemgetter version; if BaseRow __getitem__ is implemented
        // in C directly then we can use that
        //val = PyObject_GetItem(row, item);
        if (val == NULL) {
            Py_DECREF(result);
            return NULL;
        }
        PyTuple_SET_ITEM(result, i, val);
    }
    return result;
}

static PyObject *
tuplegetter_repr(tuplegetterobject *tg)
{
    PyObject *repr;
    const char *reprfmt;

    int status = Py_ReprEnter((PyObject *)tg);
    if (status != 0) {
        if (status < 0)
            return NULL;
        return PyUnicode_FromFormat("%s(...)", Py_TYPE(tg)->tp_name);
    }

    reprfmt = tg->nitems == 1 ? "%s(%R)" : "%s%R";
    repr = PyUnicode_FromFormat(reprfmt, Py_TYPE(tg)->tp_name, tg->item);
    Py_ReprLeave((PyObject *)tg);
    return repr;
}

static PyObject *
tuplegetter_reduce(tuplegetterobject *tg, PyObject *Py_UNUSED(ignored))
{
    return PyTuple_Pack(2, Py_TYPE(tg), tg->item);
}

PyDoc_STRVAR(reduce_doc, "Return state information for pickling");

static PyMethodDef tuplegetter_methods[] = {
    {"__reduce__", (PyCFunction)tuplegetter_reduce, METH_NOARGS,
     reduce_doc},
    {NULL}
};

PyDoc_STRVAR(tuplegetter_doc,
"tuplegetter(item, ...) --> tuplegetter object\n\
\n\
Return a callable object that fetches the given item(s) from its operand\n\
and returns them as a tuple.\n");

static PyTypeObject tuplegetter_type = {
    PyVarObject_HEAD_INIT(NULL, 0)
    "sqlalchemy.engine.util..tuplegetter",  /* tp_name */
    sizeof(tuplegetterobject),           /* tp_basicsize */
    0,                                  /* tp_itemsize */
    /* methods */
    (destructor)tuplegetter_dealloc,     /* tp_dealloc */
    0,                                  /* tp_vectorcall_offset */
    0,                                  /* tp_getattr */
    0,                                  /* tp_setattr */
    0,                                  /* tp_as_async */
    (reprfunc)tuplegetter_repr,          /* tp_repr */
    0,                                  /* tp_as_number */
    0,                                  /* tp_as_sequence */
    0,                                  /* tp_as_mapping */
    0,                                  /* tp_hash */
    (ternaryfunc)tuplegetter_call,       /* tp_call */
    0,                                  /* tp_str */
    PyObject_GenericGetAttr,            /* tp_getattro */
    0,                                  /* tp_setattro */
    0,                                  /* tp_as_buffer */
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_HAVE_GC,            /* tp_flags */
    tuplegetter_doc,                     /* tp_doc */
    (traverseproc)tuplegetter_traverse,          /* tp_traverse */
    0,                                  /* tp_clear */
    0,                                  /* tp_richcompare */
    0,                                  /* tp_weaklistoffset */
    0,                                  /* tp_iter */
    0,                                  /* tp_iternext */
    tuplegetter_methods,                 /* tp_methods */
    0,                                  /* tp_members */
    0,                                  /* tp_getset */
    0,                                  /* tp_base */
    0,                                  /* tp_dict */
    0,                                  /* tp_descr_get */
    0,                                  /* tp_descr_set */
    0,                                  /* tp_dictoffset */
    0,                                  /* tp_init */
    0,                                  /* tp_alloc */
    tuplegetter_new,                     /* tp_new */
    0,                                  /* tp_free */
};



static PyMethodDef module_methods[] = {
    {"safe_rowproxy_reconstructor", safe_rowproxy_reconstructor, METH_VARARGS,
     "reconstruct a Row instance from its pickled form."},
    {NULL, NULL, 0, NULL}        /* Sentinel */
};

#ifndef PyMODINIT_FUNC  /* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif


#if PY_MAJOR_VERSION >= 3

static struct PyModuleDef module_def = {
    PyModuleDef_HEAD_INIT,
    MODULE_NAME,
    MODULE_DOC,
    -1,
    module_methods
};

#define INITERROR return NULL

PyMODINIT_FUNC
PyInit_cresultproxy(void)

#else

#define INITERROR return

PyMODINIT_FUNC
initcresultproxy(void)

#endif

{
    PyObject *m;

    BaseRowType.tp_new = PyType_GenericNew;
    if (PyType_Ready(&BaseRowType) < 0)
        INITERROR;

    if (PyType_Ready(&tuplegetter_type) < 0)
        return NULL;

#if PY_MAJOR_VERSION >= 3
    m = PyModule_Create(&module_def);
#else
    m = Py_InitModule3(MODULE_NAME, module_methods, MODULE_DOC);
#endif
    if (m == NULL)
        INITERROR;

    Py_INCREF(&BaseRowType);
    PyModule_AddObject(m, "BaseRow", (PyObject *)&BaseRowType);

    Py_INCREF(&tuplegetter_type);
    PyModule_AddObject(m, "tuplegetter", (PyObject *)&tuplegetter_type);

#if PY_MAJOR_VERSION >= 3
    return m;
#endif
}
