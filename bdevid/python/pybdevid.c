/*
 * pybdevid.c - python bindings for libbdev
 *
 * Peter Jones (pjones@redhat.com)
 *
 * Copyright 2006 Red Hat, Inc.
 *
 * This software may be freely redistributed under the terms of the GNU
 * General Public License, version 2.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */
#define _GNU_SOURCE 1

#ifdef _POSIX_C_SOURCE
#define ARGHA _POSIX_C_SOURCE
#undef _POSIX_C_SOURCE
#endif
#undef _GNU_SOURCE
#include <Python.h>
#ifdef ARGHA
#undef _POSIX_C_SOURCE
#define _POSIX_C_SOURCE ARGHA
#undef ARGHA
#endif
#define _GNU_SOURCE 1

#include <bdevid.h>

#include "pybdevid.h"

#define PYBD_ARGS (METH_VARARGS|METH_KEYWORDS)

static void
pybd_bdevid_dealloc(PyBdevidObject *bd)
{
    if (bd->bdevid) {
        bdevid_destroy(bd->bdevid);
        bd->bdevid = NULL;
    }
    Py_XDECREF(bd->env);
    PyObject_Del(bd);
}

static int
pybd_bdevid_init_method(PyObject *self, PyObject *args, PyObject *kwds)
{
    char *kwlist[] = {"env", NULL};
    PyBdevidObject *bd = (PyBdevidObject *)self;
    char *env = NULL;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "|O!:bdevid.__init__", kwlist,
            &PyString_Type, &bd->env))
        return -1;

    if (bd->env) {
        Py_INCREF(bd->env);
        env = PyString_AsString(bd->env);
    }
    
    if (!(bd->bdevid = bdevid_new(env))) {
        PyErr_SetFromErrno(PyExc_SystemError);
        return -1;
    }
    return 0;
}

static PyObject *
pybd_bdevid_str_method(PyObject *self)
{
    PyBdevidObject *bd = (PyBdevidObject *)self;
    char *env = NULL;

    if (bd->env) {
        env = PyString_AsString(bd->env);
        return PyString_FromFormat("<bdevid env=\"%s\">", env);
    }

    return PyString_FromFormat("<bdevid>");
}

static int
pybd_bdevid_compare(PyBdevidObject *self, PyBdevidObject *other)
{
    return self < other ? -1 : self > other ? 1 : 0;
}

static long
pybd_bdevid_hash(PyObject *self)
{
    return (long)self;
}

static PyObject *
pybd_bdevid_get(PyObject *self, void *data)
{
    PyBdevidObject *bd = (PyBdevidObject *)self;
    const char *attr = (const char *)data;

    if (!strcmp(attr, "path")) {
        char *str = NULL;

        if (!(str = bdevid_path_get(bd->bdevid))) {
            PyErr_SetFromErrno(PyExc_SystemError);
            return NULL;
        }
        return PyString_FromString(str);
    }
    return NULL;
}

static int
pybd_bdevid_set(PyObject *self, PyObject *value, void *data)
{
    PyBdevidObject *bd = (PyBdevidObject *)self;
    const char *attr = (const char *)data;

    if (!strcmp(attr, "path")) {
        char *path;
        
        if (!(path = PyString_AsString(value)))
            return -1;

        if (bdevid_path_set(bd->bdevid, path) < 0) {
            PyErr_SetFromErrno(PyExc_SystemError);
            return -1;
        }
        return 0;
    }
    return -1;
}

static struct PyGetSetDef pybd_bdevid_getseters[] = {
    {"path", (getter)pybd_bdevid_get, (setter)pybd_bdevid_set, "path", "path"},
    {NULL},
};

static PyObject *
pybd_bdevid_load(PyObject *self, PyObject *args, PyObject *kwds)
{
    char *kwlist[] = {"name", NULL};
    PyBdevidObject *bd = (PyBdevidObject *)self;
    char *name;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s:bdevid.load", kwlist,
            &name))
        return NULL;

    if (bdevid_module_load(bd->bdevid, name) < 0) {
        PyErr_SetFromErrno(PyExc_SystemError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static PyObject *
pybd_bdevid_unload(PyObject *self, PyObject *args, PyObject *kwds)
{
    char *kwlist[] = {"name", NULL};
    PyBdevidObject *bd = (PyBdevidObject *)self;
    char *name;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s:bdevid.unload", kwlist,
            &name))
        return NULL;

    if (bdevid_module_unload(bd->bdevid, name) < 0) {
        PyErr_SetFromErrno(PyExc_SystemError);
        return NULL;
    }

    Py_INCREF(Py_None);
    return Py_None;
}

static int
_pybd_bdevid_probe_visitor(struct bdevid_probe_result *result, void *priv)
{
    PyObject *list = priv;
    char *attrs[] = {"module", "probe", "vendor", "model", "unique_id", NULL};
    int i, add = 0;
    PyObject *dict;

    if (!(dict = PyDict_New()))
        return -1;

    for (i = 0; attrs[i]; i++) {
        const char *valuestr = NULL;
        PyObject *key, *value;
    

        if (!(key = PyString_FromString(attrs[i])))
            goto err;

        if (!(valuestr = bdevid_pr_getattr(result, attrs[i]))) {
            value = Py_None;
        } else {
            if (!(value = PyString_FromString(valuestr)))
                goto err;
        }

        if (PyDict_SetItem(dict, key, value) < 0)
            goto err;

        add = 1;
        continue;
err:
        Py_XDECREF(key);
        Py_XDECREF(value);
        Py_DECREF(dict);

        return -1;
    }

    if (add && PyList_Append(list, dict) < 0)
        return -1;
    return 0;
}

static PyObject *
pybd_bdevid_probe(PyObject *self, PyObject *args, PyObject *kwds)
{
    char *kwlist[] = {"device", NULL};
    PyBdevidObject *bd = (PyBdevidObject *)self;
    PyObject *list;
    char *device;

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "s:bdevid.probe", kwlist,
            &device))
        return NULL;

    if (!(list = PyList_New(0)))
        return NULL;

    if (bdevid_probe(bd->bdevid, device, 
            _pybd_bdevid_probe_visitor, list) < 0) {
        Py_DECREF(list);
        if (!PyErr_Occurred())
            PyErr_SetFromErrno(PyExc_SystemError);
        return NULL;
    }

    return list;
}

static struct PyMethodDef pybd_bdevid_methods[] = {
    {"load", (PyCFunction) pybd_bdevid_load, PYBD_ARGS},
    {"unload", (PyCFunction) pybd_bdevid_unload, PYBD_ARGS},
    {"probe", (PyCFunction) pybd_bdevid_probe, PYBD_ARGS},
    {NULL}
};

PyTypeObject PyBdevid_Type = {
    PyObject_HEAD_INIT(NULL)
    .tp_name = "bdevid",
    .tp_basicsize = sizeof (PyBdevidObject),
    .tp_dealloc = (destructor)pybd_bdevid_dealloc,
    .tp_getset = pybd_bdevid_getseters,
    .tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_CHECKTYPES |
                Py_TPFLAGS_BASETYPE,
    .tp_methods = pybd_bdevid_methods,
    .tp_compare = (cmpfunc)pybd_bdevid_compare,
    .tp_hash = pybd_bdevid_hash,
    .tp_init = pybd_bdevid_init_method,
    .tp_str = pybd_bdevid_str_method,
    .tp_new = PyType_GenericNew,
};

static PyMethodDef pybd_functions[] = {
    {NULL, NULL}
};

PyMODINIT_FUNC
initbdevid(void)
{
    PyObject *m;

    m = Py_InitModule("bdevid", pybd_functions);

    if (PyType_Ready(&PyBdevid_Type) < 0)
        return;
    Py_INCREF(&PyBdevid_Type);
    PyModule_AddObject(m, "bdevid", (PyObject *) &PyBdevid_Type);

    return;
}

/*
 * vim:ts=8:sw=4:sts=4:et
 */
