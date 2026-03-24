/*
  This library is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 3 of the License, or
  (at your option) any later version.

  This library is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this library; if not, see <http://www.gnu.org/licenses/>.
*/


#ifndef KOSKARI_NAMESPACE_H
#define KOSKARI_NAMESPACE_H


#include <Python.h>


/* === NamespaceSpec === */


extern PyTypeObject KosNamespaceSpecType;


typedef struct KosNamespaceSpec {
  PyObject_HEAD

  PyObject *ns_name;
  PyObject *local_syms;
  PyObject *closure_syms;
  PyObject *global_syms;
} KosNamespaceSpec;


PyObject *KosNamespaceSpec_New(
    PyObject *name,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms);


#define KosNamespaceSpec_NAME(nspec)            \
  (((KosNamespaceSpec *) nspec)->ns_name)

#define KosNamespaceSpec_LOCAL_SYMS(nspec)      \
  (((KosNamespaceSpec *) nspec)->local_syms)

#define KosNamespaceSpec_CLOSURE_SYMS(nspec)    \
  (((KosNamespaceSpec *) nspec)->closure_syms)

#define KosNamespaceSpec_GLOBAL_SYMS(nspec)     \
  (((KosNamespaceSpec *) nspec)->global_syms)

#define KosNamespaceSpec_CheckExact(obj)                \
  ((obj) && ((obj)->ob_type == &KosNamespaceSpecType))


/* === Namespace === */


extern PyTypeObject KosNamespaceType;


typedef struct KosNameBind {
  PyObject *name;

  PyObject *held;   // raw? value: reference(value)

  int raw;          // 0 or 1, indicating whether the held value is
                    // wrapped in a reference or not.
} KosNameBind;


#define KosNameBind_NAME(nb)  (((KosNameBind *)(nb))->name)
#define KosNameBind_HELD(nb)  (((KosNameBind *)(nb))->held)
#define KosNameBind_RAW(nb)   (((KosNameBind *)(nb))->raw)


typedef struct KosNamespace {
  PyObject_VAR_HEAD

  PyObject *name;     // PyUnicodeObject

  PyObject *parent;   // KosNamespace
  PyObject *binds;    // PyDictObject

  size_t fast_count;    // count of fast allocated with this Namespace
  KosNameBind fast[0];  // fast entries
} KosNamespace;


PyObject *KosNamespace_New(
    PyObject *parent,
    PyObject *spec);


PyObject *KosNamespace_NewFull(
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms);


/**
   Build a namespace from the given NamespaceSpec and parent.
   Same as NSPEC.get_namespace(parent). New reference or NULL on error.
*/
PyObject *KosNamespace_FromSpec(PyObject *nspec, PyObject *parent);


/**
   Copy the top-level namespace only. Does not copy the parent chain.
   Returns a new namespace with the same shape and closure refs; local
   slots are left empty. New reference or NULL on error.
*/
PyObject *KosNamespace_Copy(PyObject *template_ns);


/**
   Looks up Ref matching key.

   Borrowed reference, or NULL with error set
 */
PyObject *KosNamespace_Ref(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key.

   Borrowed reference, or NULL. Does not set error if no match is found,
   but may set an error if some other failure occured in the lookup.
 */
PyObject *KosNamespace_REF(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key.

   Borrowed reference, Py_None, or NULL. Does not set error if no
   match is found, but may set an error if some other failure occurred
   in the lookup.

   The Py_None value is used as a sentinel to indicate that the
   namespace has the binding, but it is in fast raw mode. The
   binding's value will be lazily wrapped in a KosSymbolicRef instance
   if accessed via KosNamespace_REF, KosNamespace_Ref, or
   KosNamespace_FastRef
 */
PyObject *KosNamespace_LOOSE_REF(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key. If it exists deref it.

   New reference, or NULL with error set
*/
PyObject *KosNamespace_Deref(PyObject *ns, PyObject *key);


/**
   Looks up Ref matching key. If not found in ns, check the parent of
   ns, and so forth.

   If a match is discovered, a borrowed reference is stored in the found
   pointer. Otherwise it is left unchanged.

   returns -1 if an error occurs, 0 otherwise.
 */
int KosNamespace_FindRef(PyObject *ns, PyObject *key, PyObject **found);


/**
   Test whether a Ref with the matching key is in this namespace.

   0 if key is not in ns, 1 if it is.
 */
int KosNamespace_Contains(PyObject *ns, PyObject *key);


int KosNamespace_Store(PyObject *ns, PyObject *key, PyObject *val);


int KosNamespace_Clear(PyObject *ns, PyObject *key);


/**
   Binds a name to a reference in this namespace
 */
int KosNamespace_Bind(PyObject *ns, PyObject *key, PyObject *ref);


/**
   find the fast index of a reference based on its key.

   returns index, or -1 if index not found, does not set error
 */
int KosNamespace_FastIndex(PyObject *ns, PyObject *key);


/**
   borrowed reference to a KosRef instance at fast index

   returns NULL and sets err if index is out of bounds
 */
PyObject *KosNamespace_FastRef(PyObject *ns, size_t index);


/**
   new reference to the binding's value

   returns NULL and sets err if index is out of bounds, or if
   reference was empty
 */
PyObject *KosNamespace_FastDeref(PyObject *ns, size_t index);


/**
   stores value ad fast index

   returns 0 for success, or -1 and sets err if index is out of bounds
 */
int KosNamespace_FastStore(
    PyObject *ns,
    size_t index,
    PyObject *value);


/**
   clears a reference at fast index

   returns 0 for success, or -1 and sets err if index is out of bounds
 */
int KosNamespace_FastClear(PyObject *ns, size_t index);


/**
   replaces reference at fast index with a new reference

   returns 0 for success, or -1 and sets err if index is out of bounds
 */
int KosNamespace_FastBind(
    PyObject *ns,
    size_t index,
    PyObject *ref);


#define KosNamespace_NAME(ns)               \
  (((KosNamespace *) ns)->name)

#define KosNamespace_PARENT(ns)             \
  (((KosNamespace *) ns)->parent)

#define KosNamespace_BINDS(ns)               \
  (((KosNamespace *) ns)->binds)

#define KosNamespace_FAST_COUNT(ns)             \
  (((KosNamespace *) ns)->fast_count)

#define KosNamespace_GET_FAST(ns, i)            \
  &(((KosNamespace *) ns)->fast[i])

#define KosNamespace_Check(obj)                                         \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosNamespaceType))

#define KosNamespace_CheckExact(obj)                    \
  ((obj) && ((obj)->ob_type == &KosNamespaceType))


/* === CollectorNamespace === */


extern PyTypeObject KosCollectorNamespaceType;


typedef struct KosCollectorNamespace {
  KosNamespace super;

} KosCollectorNamespace;


/**
  A KosCollectorNamespace acts just like a Namespace except that it
  will not give up its refs via KosNamespace_FindRef, preventing
  child namespaces from taking closures over its references. The most
  common use case is in the collection of members for a class
  definition, where we do not want the class members to have closures
  over each other, as that behavior conflicts heavily with the Python
  class model of members.
*/
PyObject *KosCollectorNamespace_New(
    PyObject *parent,
    PyObject *spec);


PyObject *KosCollectorNamespace_NewFull(
    PyObject *name,
    PyObject *parent,
    PyObject *local_syms,
    PyObject *closure_syms,
    PyObject *global_syms);


#define KosCollectorNamespace_Check(obj)                                \
  ((obj) && PyType_IsSubtype((obj)->ob_type, &KosCollectorNamespaceType))

#define KosCollectorNamespace_CheckExact(obj)                   \
  ((obj) && ((obj)->ob_type == &KosCollectorNamespaceType))

#define KosNamespace_SupportsFast(ns)                                   \
  (KosNamespace_CheckExact(ns) || KosCollectorNamespace_CheckExact(ns))


/* === ModuleNamespace === */


extern PyTypeObject KosModuleNamespaceType;


typedef struct KosModuleNamespace {
  KosNamespace super;
  PyObject *builtins;  // PyDictObject
} KosModuleNamespace;


PyObject *KosModuleNamespace_New(
    PyObject *name,
    PyObject *builtins);


#define KosModuleNamespace_BUILTINS(mdl)        \
  (((KosModuleNamespace *) mdl)->builtins)

#define KosModuleNamespace_CheckExact(obj)     \
  ((obj) && ((obj)->ob_type == &KosModuleNamespaceType))


/* === Module === */


extern PyTypeObject KosModuleType;


typedef struct KosModule {
  PyObject_VAR_HEAD

  PyObject *ns;         // KosNamespace
} KosModule;


/**
  A Module is an object whose attributes are backed by lookups in a
  KosNamespace instance (including KosModuleNamespace)

  A Module has one additional attribute, __dict__, which is the
  underlying KosNamespace
 */
PyObject *KosModule_New(PyObject *namespace);


#define KosModule_NAMESPACE(mdl)                \
  (((KosModule *) mdl)->ns)

#define KosModule_CheckExact(obj)     \
  ((obj) && ((obj)->ob_type == &KosModuleType))


/* === Unit lifecycle === */


int koskari_namespace_init(PyObject *mod);
int koskari_namespace_clear(PyObject *mod);


#endif


// The end.
