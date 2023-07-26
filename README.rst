=====
Slash
=====

slash (originally from *SatLAb SHell*) is a small line editor and command parser library for embedded systems. It is especially suited for serial debugging interfaces on memory constrained systems.

Main features include:

* Line editing with tab completion of commands.
* Browsable history of previously executed commands.
* Splits options into standard argc/argv format. Support for getopt option parsing.
* No need to manually maintain a global command list. Commands are automatically registered using linker sections.
* Supports statically allocated contexts and buffers. No dynamic memory allocations during use (but beware, the underlying C standard library may do so).

Building
--------

The library consists of a single ``slash.c`` file and an accompanying ``slash.h`` header file. For the simplest deployment, these can be copied to the source tree of the using project. The repository also contains a ``wscript`` file for the `Waf build system <https://waf.io/>`_ such that it can be added as subproject and used for recursion.

The repository contains an example application in ``test/slashtest.c`` that can be built using:

.. code-block:: console

    % python3 waf distclean configure build

The test application can then be run using:

.. code-block:: console

    % ./build/slashtest 
    slash % echo Hello, World!
    Hello, World!

Run the ``help`` command for a list of available commands. Partially typed commands can be completed by pressing the ``tab`` key. Pressing ``tab`` on a completed command will show usage and optional arguments.

Using
-----

Command functions take a single argument, a reference to a ``slash`` context struct:

.. code-block:: c

    static int cmd_hello(struct slash *slash)
    {
        if (slash->argc > 1) {
            printf("Hello, %s!\n", slash->argv[1]);
        } else {           
            printf("Hello, World!\n");
        }

        return SLASH_SUCCESS;
    }
    slash_command(hello, cmd_hello, "[name]", "Print greeting to the world");

The ``slash_command`` macro registers the command with name, function, optional arguments string, and a description string. Command groups and layers of subcommands can be added using other macros (see the test application for an example).

With the registry macros, slash places each command struct in its own ``.slash.<command name>`` ELF section using `variable attributes <https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html#index-section-variable-attribute>`_. Using the linker, the commands should be sorted alphabetically, and ``__start_slash`` and ``__stop_slash`` symbols should defined to mark the start and end of the command array:

.. code-block:: c

    .rodata : {
        __start_slash = .;
        KEEP(*(SORT_BY_NAME(.slash.*)));
        __stop_slash = .;
    }

The sections can be placed in read-only memory, such as microcontroller flash. The test application uses the linker script in ``linkerscript/slash.ld`` as an overlay to the default Linux linker script.

The slash context and the line and history buffers can either be dynamically allocated using ``slash_create()`` or statically allocated and initialized using ``slash_init()``.  Dynamically allocated contexts can be freed using ``slash_destroy()``. The context and the buffers must resize in writable memory.

License
-------

The library is released under the MIT license. See the ``LICENSE`` file for the full license text.
