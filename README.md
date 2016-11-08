# protobuf
A protobuf extention for php7

Protocol Buffers types map to PHP types as follows (x86_64):

    | Protocol Buffers | PHP    |
    | ---------------- | ------ |
    | double           | float  |
    | float            |        |
    | ---------------- | ------ |
    | int32            | int    |
    | int64            |        |
    | uint32           |        |
    | uint64           |        |
    | sint32           |        |
    | sint64           |        |
    | fixed32          |        |
    | fixed64          |        |
    | sfixed32         |        |
    | sfixed64         |        |
    | ---------------- | ------ |
    | bool             | bool   |
    | ---------------- | ------ |
    | string           | string |
    | bytes            |        |


Protocol Buffers types map to PHP types as follows (x86):

    | Protocol Buffers | PHP                         |
    | ---------------- | --------------------------- |
    | double           | float                       |
    | float            |                             |
    | ---------------- | --------------------------- |
    | int32            | int                         |
    | uint32           |                             |
    | sint32           |                             |
    | fixed32          |                             |
    | sfixed32         |                             |
    | ---------------- | --------------------------- |
    | int64            | if val <= PHP_INT_MAX       |
    | uint64           | then value is stored as int |
    | sint64           | otherwise as double         |
    | fixed64          |                             |
    | sfixed64         |                             |
    | ---------------- | --------------------------- |
    | bool             | bool                        |
    | ---------------- | --------------------------- |
    | string           | string                      |
    | bytes            |                             |


Not set value is represented by *null* type. To unset value just set its value to *null*.
