// Python Multi-Process Dictionary by gatopeich

#include <Python.h>

#include <algorithm>
#include <boost/interprocess/managed_shared_memory.hpp>
#include <boost/interprocess/containers/map.hpp>
#include <boost/interprocess/allocators/allocator.hpp>
#include <boost/python.hpp>
#include <boost/python/module.hpp>
#include <boost/python/def.hpp>
#include <functional>
#include <utility>

using namespace boost::interprocess;


namespace
{
    PyObject *logging = PyImport_ImportModuleNoBlock("logging");
    void log(const char* level, const char* message)
    {
        PyObject *string = Py_BuildValue("s", message);
        PyObject_CallMethod(logging, level, "O", string);
        Py_DECREF(string);
    }
    inline void log_debug(const char *message) { log("debug", message); }
    inline void log_info(const char *message) { log("info", message); }
    inline void log_warning(const char *message) { log("warning", message); }
    inline void log_error(const char *message) { log("warnerror", message); }
}

struct MPDictObject : PyObject
{
    using KeyType = std::string;
    using ValueType = std::string;
    using KeyValueType = std::pair<const KeyType, ValueType>;
    using ShmemAllocator = allocator<KeyValueType, managed_shared_memory::segment_manager>;
    using SharedMap = map<KeyType, ValueType, std::less<KeyType>, ShmemAllocator>;

    const std::string map_name, filename;
    managed_shared_memory segment;
    ShmemAllocator alloc_inst;
    SharedMap *map_;
    static const unsigned PAGE = 4096;

    MPDictObject(const char *name, size_t datasize, const char *filename)
        // Must clear on init, but why?
        : map_name(name), filename((shared_memory_object::remove(filename),filename))
        , segment(create_only, filename, PAGE*(2+(datasize/PAGE)))
        , alloc_inst(segment.get_segment_manager())
        , map_(segment.construct<SharedMap>(name)(std::less<KeyType>(), alloc_inst))
    {}

    ~MPDictObject() { shared_memory_object::remove(filename.c_str()); }

    PyObject *get(const std::string &key)
    {
        const auto it = map_->find(key);
        if (it != map_->end())
            return PyUnicode_FromStringAndSize(it->second.c_str(), it->second.size());
        Py_RETURN_NONE;
    }

    bool set(const std::string && key, const std::string && value)
    {
        try {
            auto [it, ok] = map_->insert(KeyValueType(key, value)); // TODO: How is the allocator being used?
            if (!ok)
                it->second = value; // Have to replace
            return ok;
        } catch (...) {
            log_error("Out of shared memory...");
            return PyErr_NoMemory();
        }
    }

    PyObject * del(const std::string &key)
    {
        if (map_->erase(key))
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    // TODO: Return iterator
    PyObject * keys()
    {
        unsigned i=0, len = map_->size();
        PyObject *keys = PyTuple_New(len);
        if (!keys)
            return PyErr_NoMemory();
        for (auto it = map_->cbegin(); it != map_->cend() && i < len; ++it)
        {
            if (!PyTuple_SetItem(keys, i++, PyUnicode_FromString(it->first.c_str())))
                continue;
            PyErr_BadInternalCall();
            Py_DECREF(keys);
            return NULL;
        }
        return keys;
    }
};

static void
MPDict_dealloc(MPDictObject *self)
{
    self->~MPDictObject();
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
MPDict_new(PyTypeObject *type, PyObject * args, PyObject *kwds)
{
    return type->tp_alloc(type, 0);
}

static int
MPDict_init(MPDictObject *self, PyObject * args)
{
    const char *name, *filename = "mpdict";
    unsigned size;
    if (!PyArg_ParseTuple(args, "sI|s", &name, &size, &filename))
        return -1;
    new (self) MPDictObject(name, size, filename);
    return 0;
}

namespace
{
    // PyMappingMethods

    Py_ssize_t MPDict_lenfunc(MPDictObject *self)
    {
        return self->map_->size();
    }

    PyObject * MPDict_binary_get(MPDictObject *self, PyObject *key)
    {
        const char *k;
        Py_ssize_t kl;
        if (!(k = PyUnicode_AsUTF8AndSize(key, &kl)))
            return NULL;
        return self->get(std::string(k, kl));
    }

    int MPDict_objobj_set(MPDictObject *self, PyObject *key, PyObject *value)
    {
        const char *k, *v; // TODO: Read value as bytes-like (Py_buffer)
        Py_ssize_t kl, vl;
        if (!(k = PyUnicode_AsUTF8AndSize(key, &kl)))
            return PyErr_BadArgument();
        if (!(v = PyUnicode_AsUTF8AndSize(value, &vl)))
            return PyErr_BadArgument();
        self->set(std::string(k,kl), std::string(v,vl));
        return 0;
    }

    PyMappingMethods MPDict_mapping = {
        (lenfunc)MPDict_lenfunc,          // lenfunc PyMappingMethods.mp_length
        (binaryfunc)MPDict_binary_get,    // binaryfunc PyMappingMethods.mp_subscript
        (objobjargproc)MPDict_objobj_set, // objobjargproc PyMappingMethods.mp_ass_subscript
    };


    // Custom methods

    PyObject * MPDict_del(MPDictObject *self, PyObject *args)
    {
        const char *key;
        if (!PyArg_ParseTuple(args, "s", &key))
            return NULL;
        return self->del(key);
    }

    PyObject * MPDict_keys(MPDictObject *self, PyObject * args)
    {
        return self->keys();
    }

    PyMethodDef MPDict_methods[] = {
        {"del", (PyCFunction)MPDict_del, METH_VARARGS, "Delete MPDict[key]"},
        {"keys",(PyCFunction)MPDict_keys,METH_VARARGS, "Returns a tuple with 'max' number of keys (default 999)"},
        {NULL} /* Sentinel */
    };

    PyTypeObject MPDictType = {
        PyVarObject_HEAD_INIT(NULL, 0)
    };

    PyModuleDef mpdict_module = {
        PyModuleDef_HEAD_INIT,
        m_name : "mpdict",
        m_doc : "Multi-Process Dictionary by gatopeich.",
        m_size : -1,
    };
}

PyMODINIT_FUNC
PyInit_mpdict(void)
{
    MPDictType.tp_name = "mpdict.MPDict";
    MPDictType.tp_basicsize = sizeof(MPDictObject);
    MPDictType.tp_itemsize = 0;
    MPDictType.tp_dealloc = (destructor)MPDict_dealloc;
    MPDictType.tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
    MPDictType.tp_doc = "MPDict objects";
    MPDictType.tp_methods = MPDict_methods;
    MPDictType.tp_members = NULL;
    MPDictType.tp_init = (initproc)MPDict_init;
    MPDictType.tp_new = MPDict_new;
    MPDictType.tp_as_mapping = &MPDict_mapping;
    if (PyType_Ready(&MPDictType) < 0)
        return NULL;

    PyObject *module = PyModule_Create(&mpdict_module);
    if (module)
    {
        Py_INCREF(&MPDictType);
        PyModule_AddObject(module, "MPDict", (PyObject *)&MPDictType);
    }
    return module;
}
