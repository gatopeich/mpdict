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

struct MPDict
{
    using KeyType = std::string;
    using ValueType = std::string;
    using KeyValueType = std::pair<const KeyType, ValueType>;
    using ShmemAllocator = allocator<KeyValueType, managed_shared_memory::segment_manager>;
    using MyMap = map<KeyType, ValueType, std::less<KeyType>, ShmemAllocator>;

    const std::string map_name, space_name;
    managed_shared_memory segment;
    ShmemAllocator alloc_inst;
    MyMap *mymap;
    static const unsigned PAGE = 4096;

    MPDict(const char *name, size_t datasize, const char *space)
        : map_name(name), space_name(space)
        , segment(create_only, space, PAGE*(2+(datasize/PAGE)))
        , alloc_inst(segment.get_segment_manager())
        , mymap(segment.construct<MyMap>(name)(std::less<KeyType>(), alloc_inst))
    {
        // Must clear on init, but why?
        shared_memory_object::remove(space_name.c_str());
    }
    ~MPDict() {
        shared_memory_object::remove(space_name.c_str());
    }

    PyObject* get(const std::string & key)
    {
        const auto it = mymap->find(key);
        if (it != mymap->end())
            return PyUnicode_FromStringAndSize(it->second.c_str(), it->second.size());
        Py_RETURN_NONE;
    }

    PyObject* set(const std::string && key, const std::string && value)
    {
        try {
            auto [it, ok] = mymap->insert(KeyValueType(key, value)); // TODO: How is the allocator being used?
            if (ok)
                Py_RETURN_FALSE; // Did not replace
            it->second = value;
            Py_RETURN_TRUE;
        } catch (...) {
            log_error("Out of shared memory...");
            PyErr_NoMemory();
            return PyErr_NoMemory();
        }
    }

    PyObject *del(const std::string &key)
    {
        if (mymap->erase(key))
            Py_RETURN_TRUE;
        Py_RETURN_FALSE;
    }

    // TODO: Return iterator
    PyObject *keys(size_t max = 999)
    {
        int len = std::min(mymap->size(), max);
        PyObject *keys = PyTuple_New(len);
        if (keys) {
            int i = 0;
            auto it = mymap->begin(), e = mymap->end();
            while (it != e && i < len)
            {
                if (PyTuple_SetItem(keys, i++, PyUnicode_FromString(it->first.c_str()))) {
                    PyErr_BadInternalCall();
                    Py_DECREF(keys);
                    return NULL;
                }
                ++it;
            }
        }
        return keys;
    }
};

typedef struct
{
    PyObject_HEAD;
    MPDict *instance;
} MPDictObject;

static void
MPDict_dealloc(MPDictObject *self)
{
    delete self->instance;
    Py_TYPE(self)->tp_free((PyObject *)self);
}

static PyObject *
MPDict_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    MPDictObject *self = (MPDictObject *)type->tp_alloc(type, 0);
    self->instance = NULL;
    return (PyObject *)self;
}

static int
MPDict_init(MPDictObject *self, PyObject *args)
{
    const char *name, *space = "mpdict";
    unsigned size;
    if (!PyArg_ParseTuple(args, "sI|s", &name, &size, &space))
        return -1;
    self->instance = new MPDict(name, size, space);
    return 0;
}

static PyObject *
MPDict_get(MPDictObject *self, PyObject *args)
{
    const char *key;
    if (!PyArg_ParseTuple(args, "s", &key))
        return NULL;
    return self->instance->get(key);
}

static PyObject *
MPDict_set(MPDictObject *self, PyObject *args)
{
    const char *key, *value; // TODO: Read value as bytes-like (Py_buffer)
    if (!PyArg_ParseTuple(args, "ss", &key, &value))
        return NULL;
    return self->instance->set(key, value);
}

static PyObject *
MPDict__get(MPDictObject *self, PyObject *key)
{
    const char *k;
    Py_ssize_t kl;
    if (!(k = PyUnicode_AsUTF8AndSize(key, &kl)))
        return NULL;
    return self->instance->get(std::string(k,kl));
}

static PyObject *
MPDict__set(MPDictObject *self, PyObject *key, PyObject *value)
{
    const char *k, *v; // TODO: Read value as bytes-like (Py_buffer)
    Py_ssize_t kl, vl;
    if (!(k = PyUnicode_AsUTF8AndSize(key, &kl)))
        return NULL;
    if (!(v = PyUnicode_AsUTF8AndSize(value, &vl)))
        return NULL;
    return self->instance->set(std::string(k,kl), std::string(v,vl));
}

static PyObject *
MPDict_del(MPDictObject *self, PyObject *args)
{
    const char *key;
    if (!PyArg_ParseTuple(args, "s", &key))
        return NULL;
    return self->instance->del(key);
}

static PyObject *
MPDict_len(MPDictObject *self)
{
    return PyLong_FromSize_t(self->instance->mymap->size());
}

static PyObject *
MPDict_keys(MPDictObject *self, PyObject *args)
{
    size_t max = 999;
    if (!PyArg_ParseTuple(args, "|I", &max))
        return NULL;
    return self->instance->keys(max);
}

static PyMethodDef MPDict_methods[] = {
    {"get", (PyCFunction)MPDict_get, METH_VARARGS, "Get MPDict[key]"},
    {"set", (PyCFunction)MPDict_set, METH_VARARGS, "Set MPDict[key] = value"},
    {"del", (PyCFunction)MPDict_del, METH_VARARGS, "Delete MPDict[key]"},
    {"len", (PyCFunction)MPDict_len, METH_NOARGS, "len(MPDict)"},
    {"keys", (PyCFunction)MPDict_keys, METH_VARARGS, "Returns a tuple with 'max' number of keys (default 999)"},
    {NULL} /* Sentinel */
};

static PyTypeObject MPDictType = {
    PyVarObject_HEAD_INIT(NULL, 0)};

static PyModuleDef mpdict_module = {
    PyModuleDef_HEAD_INIT,
    m_name : "mpdict",
    m_doc : "Multi-Process Dictionary by gatopeich.",
    m_size : -1,
};

static PyMappingMethods MPDict_mapping = {
    (lenfunc)MPDict_len,       // lenfunc PyMappingMethods.mp_length
    (binaryfunc)MPDict__get,    // binaryfunc PyMappingMethods.mp_subscript
    (objobjargproc)MPDict__set, // objobjargproc PyMappingMethods.mp_ass_subscript
};

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
