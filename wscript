#!/usr/bin/env python
# encoding: utf-8

APPNAME = 'slash'
VERSION = '0.1.0'

def options(ctx):
    gr = ctx.add_option_group('slash options')
    gr.add_option('--slash-disable-exit', action='store_true', help='Disable exit command')

def configure(ctx):
    ctx.check(header_name='termios.h', features='c cprogram', mandatory=False)
    ctx.define_cond('SLASH_NO_EXIT', ctx.options.slash_disable_exit)

def build(ctx):
    ctx.objects(
        target   = APPNAME,
        source   = 'src/slash.c',
        includes = 'include',
        export_includes = 'include')
