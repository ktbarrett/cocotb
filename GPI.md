# Generic Procedural Interface

The Generic Procedural Interface (GPI) is a procedural interface for interacting with running simulation similar to the VPI and VHPI.
Unlike the aforementioned procedural interfaces, it is designed to be language-agnostic and focuses on co-simulation functionality,
eschewing language-specific reflection features.

## Object Model

Any simulator object supports the following properties.

| Property | Description |
|-|-|
| `GPI_OBJ_TYPE` | The GPI type of the object. |
| `GPI_NAME` | The name of the object. |
| `GPI_PATH` | The full path to the object. |

### Integer

`GPI_OBJ_TYPE`: `GPI_TYPE_INTEGER`

This represents all two-state integer-like objects.
For example `integer`, `natural`, and `positive` from VHDL or `int`, `shortint`, `longint`, etc. from Verilog.

| Property | Description |
|-|-|
| `GPI_SIZE` | The number of bits in the integer. |
| `GPI_CONST` | Whether the object is mutable. |
| `GPI_SIGNED` | Whether the integer is signed or unsigned. |

* `gpi_get_value_int`: Returns the value as a 64-bit integer.
* `gpi_get_value_str`: Returns the value as a N-bit binary string.
* `gpi_set_value_int`: Sets the value using a 64-bit integer. Fails if the value is too large to fit in the integer.
* `gpi_set_value_str`: Sets the value using an N-bit binary string. Fails if the binary string doesn't match the size of integer.

### Real

`GPI_OBJ_TYPE`: `GPI_TYPE_REAL`

This represents IEEE floating point like values, e.g. `real` from VHDL and `real` and `shortreal` from Verilog.

| Property | Description |
|-|-|
| `GPI_SIZE` | The number of bits in the real. |
| `GPI_CONST` | Whether the object is mutable. |

* `gpi_get_value_real`: Returns the value as a IEEE double-precision floating point value.
* `gpi_set_value_real`: Sets the value using a IEEE double-precision floating point value. Setting simulator objects that are smaller than a IEEE double-precision floating point value will cause a conversion to occur which may change the value. This is undefined. Fails if values are too large or too small.

### String

`GPI_OBJ_TYPE`: `GPI_TYPE_STRING`

This represents the kinds of strings that have a dynamic length.
For example, strings in Verilog and access type strings in VHDL.

| Property | Description |
|-|-|
| `GPI_SIZE` | The number of bytes in the string. |
| `GPI_CONST` | Whether the object is mutable. |
| `GPI_LEFT` | The left bound. |
| `GPI_DIRECTION` | The direction of the range of the string. |
| `GPI_RIGHT` | The right bound. |

* `gpi_get_value_str`: Returns the value as a byte string.
* `gpi_set_value_str`: Sets the value with a byte string. The simulator object is resized to match the length of the value being passed to the function.

### Fixed String

This represents the kinds of strings that are fixed in length,
such as VHDL strings with static bounds.

`GPI_OBJ_TYPE`: `GPI_TYPE_FIXED_STRING`

| Property | Description |
|-|-|
| `GPI_SIZE` | The number of bytes in the string. |
| `GPI_CONST` | Whether the object is mutable. |
| `GPI_LEFT` | The left bound. |
| `GPI_DIRECTION` | The direction of the range of the string. |
| `GPI_RIGHT` | The right bound. |

* `gpi_get_value_str`: Returns the value as a byte string.
* `gpi_set_value_str`: Sets the value with a byte string. Strings passed to the function that are smaller than the simulator object are padded with `NUL` bytes. Strings that are longer than the simulator object cause the function to fail.

### Enum

### Time

### Physical Types

### Module

### Logic Scalar

### Packed Array

### Packed Struct

### Unpacked Array

### Unpacked Struct

### Generate Array

### Generate Block
