#!/usr/bin/env python
# encoding: utf-8

APPNAME = 'slash'
VERSION = '0.1.0'

top = '.'
build = 'build'

def options(ctx):
    gr = ctx.add_option_group('slash options')
    gr.add_option('--slash-disable-exit', action='store_true', help='Disable exit command')

def configure(ctx):
    # Load tool and set CFLAGS if not being recursed
    if len(ctx.stack_path) < 2:
        ctx.load('gcc')
        ctx.env.CFLAGS = [
            '-std=gnu11', '-Os', '-gdwarf',
            '-Wall',
            '-Wextra',
            '-Wshadow',
            '-Wstrict-prototypes',
            '-Wmissing-prototypes',
            '-Wno-unused-parameter']

    ctx.check(header_name='termios.h', features='c cprogram', mandatory=False, define_name='SLASH_HAVE_TERMIOS_H')
    ctx.define_cond('SLASH_NO_EXIT', ctx.options.slash_disable_exit)

def build(ctx):
    if len(ctx.stack_path) < 2:
        ctx.load('gcc')

    ctx.objects(
        target   = APPNAME,
        source   = 'src/slash.c',
        includes = 'include',
        export_includes = 'include')

    if len(ctx.stack_path) < 2:
        ctx.program(
            target   = APPNAME + 'test',
            source   = 'test/slashtest.c',
            use      = APPNAME)
