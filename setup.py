#!/usr/bin/python
from distutils.core import setup, Extension

setup(
    name            = 'mpdict',
    version         = '0.1',
    author          = 'gatopeich',
    author_email    = 'gatopeich@pm.me',
    description     = 'Multi-process Dictionary',
    license         = "BSD",
    keywords        = "shared memory dict map boost interprocess concurrent",
    url             = "http://github.com/gatopeich/mpdict",
    ext_modules = [
        Extension('mpdict',
            language = 'c++',
            sources = ['mpdict.cpp'],
            extra_compile_args = '-std=gnu++1z -Wall -Werror'.split(),
            libraries = ['stdc++', 'boost_container', 'rt'])
    ]
)
