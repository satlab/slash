#!/usr/bin/env python
# encoding: utf-8

APPNAME = 'slash'
VERSION = '0.2.0'

top = '.'
build = 'build'

def options(ctx):
    if len(ctx.stack_path) < 2:
        ctx.load('compiler_c waf_unit_test')

    gr = ctx.add_option_group('slash options')
    gr.add_option('--slash-disable-exit', action='store_true', help='Disable exit command')
    gr.add_option('--slash-disable-linkerscript', action='store_true', help='Disable linker script insert')

def configure(ctx):
    # Load tool and set CFLAGS if not being recursed
    if len(ctx.stack_path) < 2:
        ctx.load('compiler_c waf_unit_test')
        ctx.env.CFLAGS = [
            '-std=gnu11', '-Os', '-gdwarf',
            '-Wall',
            '-Wextra',
            '-Wshadow',
            '-Wstrict-prototypes',
            '-Wmissing-prototypes',
            '-Wno-unused-parameter']

        ctx.check_cfg(package='cmocka', uselib_store='cmocka', args=['--cflags', '--libs'])

    # Insert linker script if not explicitly disabled
    # or another linkerscript has been defined
    if (not ctx.options.slash_disable_linkerscript and
        not [x for x in ctx.env.LDFLAGS if x.startswith('-T')]):
        ctx.env.LDFLAGS += [
            '-Wl,-L{}'.format(ctx.path.find_node("linkerscript")),
            '-Tslash.ld']

    ctx.check(header_name='termios.h', features='c cprogram', mandatory=False, define_name='SLASH_HAVE_TERMIOS_H')
    ctx.define_cond('SLASH_NO_EXIT', ctx.options.slash_disable_exit)

def build(ctx):
    if len(ctx.stack_path) < 2:
        ctx.load('compiler_c waf_unit_test')
        ctx.program(
            target   = APPNAME + '-example',
            source   = 'test/example.c',
            use      = APPNAME)

        ctx.program(
            features = 'test',
            target = APPNAME + '-test',
            source = 'test/test.c',
            use = [APPNAME, 'cmocka'])

        from waflib.Tools import waf_unit_test
        ctx.add_post_fun(waf_unit_test.summary)

    ctx.objects(
        target   = APPNAME,
        source   = 'src/slash.c',
        includes = 'include',
        export_includes = 'include')
