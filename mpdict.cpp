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



namespace mpdict
{

    PyModuleDef mpdict_module = {
        PyModuleDef_HEAD_INIT,
        m_name : "mpdict",
        m_doc : "Multi-Process Dictionary by gatopeich.",
        m_size : -1,
    };

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

    struct MPDictType : PyTypeObject { MPDictType(); } mpdict_type;
    
    using namespace boost::interprocess;
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
                // TODO: How is the allocator being used?
                auto [it, ok] = map_->emplace(key, value);
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

    void MPDict_dealloc(MPDictObject *self)
    {
        self->~MPDictObject();
        Py_TYPE(self)->tp_free(self);
    }

    int MPDict_init(MPDictObject *self, PyObject * args)
    {
        const char *name, *filename = "mpdict";
        unsigned size;
        if (!PyArg_ParseTuple(args, "sI|s", &name, &size, &filename))
            return -1;
        new (self) MPDictObject(name, size, filename); // Note this SHOULD NOT touch inherited PyObject!
        return 0;
    }

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

    // Other methods

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

    // MPDictIterator class
    struct MPDictItType : PyTypeObject { MPDictItType(); } mpdictit_type;

    struct MPDictItObject : PyObject
    {
        MPDictObject *dict;
        MPDictObject::SharedMap::iterator it;
    };

    int MPDictIt_init(MPDictItObject *self, PyObject *args, PyObject *kwargs)
    {
        MPDictObject *dict;
        if (!PyArg_UnpackTuple(args, __func__, 1, 1, &dict) || !PyObject_TypeCheck(dict, &mpdict_type))
            return PyErr_BadArgument();
        Py_INCREF(dict);
        self->dict = dict;
        self->it = dict->map_->begin();
        return 0;
    }

    void MPDictIt_dealloc(MPDictItObject *self)
    {
        Py_XDECREF(self->dict);
        Py_TYPE(self)->tp_free(self);
    }

    PyObject * MPDictIt_next(MPDictItObject *self)
    {
        if (!self->dict || self->it == self->dict->map_->end())
            return NULL;
        PyObject* key = PyUnicode_FromString(self->it->first.c_str());
        if (++self->it == self->dict->map_->end()) {
            Py_XDECREF(self->dict);
        }
        return key;
    }

    PyObject* MPDict_GetIter(PyObject *self)
    {
        PyObject *args = PyTuple_Pack(1, self);
        auto *it = (MPDictItObject*)PyType_GenericNew(&mpdictit_type, args, NULL);
        if (MPDictIt_init(it, args, NULL)) {
            Py_XDECREF(it);
            it = NULL;
        }
        Py_XDECREF(args);
        return it;
    }

    MPDictType::MPDictType() : PyTypeObject{PyVarObject_HEAD_INIT(NULL, 0)}
    {
        tp_doc = "MPDict objects";
        tp_name = "mpdict.MPDict";
        tp_flags = Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE;
        tp_basicsize = sizeof(MPDictObject);
        tp_new = PyType_GenericNew;
        tp_init = (initproc)MPDict_init;
        tp_dealloc = (destructor)MPDict_dealloc;
        tp_iter = (getiterfunc)MPDict_GetIter;
        tp_as_mapping = new PyMappingMethods {
            (lenfunc)MPDict_lenfunc,
            (binaryfunc)MPDict_binary_get,
            (objobjargproc)MPDict_objobj_set};
        tp_methods = new PyMethodDef[3] {
            {"del", (PyCFunction)MPDict_del, METH_VARARGS, "Delete MPDict[key]"},
            {"keys", (PyCFunction)MPDict_keys, METH_VARARGS, "Returns a tuple with 'max' number of keys (default 999)"},
            {NULL}};
    }

    MPDictItType::MPDictItType() : PyTypeObject{PyVarObject_HEAD_INIT(NULL, 0)}
    {
        tp_doc = "MPDict iterator";
        tp_name = "mpdict.MPDictIterator";
        tp_flags = Py_TPFLAGS_DEFAULT;
        tp_basicsize = sizeof(MPDictItObject);
        tp_init = (initproc)MPDictIt_init;
        tp_dealloc = (destructor)MPDictIt_dealloc;
        tp_iter = PyObject_SelfIter;
        tp_iternext = (iternextfunc)MPDictIt_next;
    }
}

PyMODINIT_FUNC
PyInit_mpdict(void)
{
    using namespace mpdict;

    if (PyType_Ready(&mpdict_type) < 0 || PyType_Ready(&mpdictit_type))
        return NULL;

    PyObject *module = PyModule_Create(&mpdict_module);
    if (module)
    {
        Py_INCREF(&mpdict_type);
        PyModule_AddObject(module, "MPDict", (PyObject *)&mpdict_type);
    }
    return module;
}
