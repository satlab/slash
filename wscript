#!/usr/bin/env python
# encoding: utf-8

APPNAME = 'slash'
VERSION = '0.1.0'

def options(ctx):
    pass

def configure(ctx):
    ctx.check(header_name='termios.h', features='c cprogram', mandatory=False)

def build(ctx):
    ctx.objects(
        target   = APPNAME,
        source   = 'src/slash.c',
        includes = 'include',
        export_includes = 'include')
