/*
 * pybdevid.h - python bindings for libbdev
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

typedef struct {
    PyObject_HEAD

    struct bdevid *bdevid;
    PyObject *env;
} PyBdevidObject;

PyAPI_DATA(PyTypeObject) PyBdevid_Type;

#define PyBdevid_Check(op) PyObject_TypeCheck(op, &PyBdevid_Type)
#define PyBdevid_CheckExact(op) ((op)->ob_type == &PyBdevid_Type)

/*
 * vim:ts=8:sw=4:sts=4:et
 */
