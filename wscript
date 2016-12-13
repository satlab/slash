#!/usr/bin/env python
# encoding: utf-8

APPNAME = 'slash'
VERSION = '0.1.0'

def options(ctx):
    ctx.load('compiler_c')

def configure(ctx):
    ctx.load('compiler_c')
    ctx.check(header_name='termios.h', features='c cprogram', mandatory=False)

def build(ctx):
    ctx.objects(
        target   = APPNAME,
        source   = 'src/slash.c',
        includes = 'include',
        export_includes = 'include')
    ctx.program(
        target   = APPNAME + 'test',
        source   = 'src/slash.c test/slashtest.c',
        includes = 'include')
