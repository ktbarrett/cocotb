# Copyright cocotb contributors
# Licensed under the Revised BSD License, see LICENSE for details.
# SPDX-License-Identifier: BSD-3-Clause
from typing import Any, ClassVar, Dict, Generic, Type, TypeVar
from weakref import WeakValueDictionary

from cocotb.types.range import Range

T = TypeVar("T")


class GenericPackedArray(Generic[T]):
    __children_classes: WeakValueDictionary[
        Type[Any], "Type[TypeBoundPackedArray[T]]"
    ] = WeakValueDictionary()

    def __class_getitem__(cls, item: Type[Any]) -> "Type[TypeBoundPackedArray[T]]":
        if isinstance(item, type):
            try:
                return cls.__children_classes[item]
            except KeyError:
                res = type(
                    f"TypeBoundPackedArray[{item.__qualname__}]",
                    (TypeBoundPackedArray, cls),
                    {
                        "elem_type": item,
                        "_TypeBoundPackedArray__children_classes": WeakValueDictionary(),
                    },
                )
                cls.__children_classes[item] = res
                return res
        return super().__class_getitem__(item)


class TypeBoundPackedArray(GenericPackedArray[T]):
    __children_classes: Dict[slice, "Type[PackedArray[T]]"]

    elem_type: ClassVar[Type[T]]

    def __class_getitem__(cls, item: slice) -> "Type[PackedArray[T]]":
        if isinstance(item, slice):
            try:
                return cls.__children_classes[item]
            except KeyError:
                res = type(
                    f"PackedArray[{cls.elem_type.__name__}][{item}]",
                    (PackedArray, cls),
                    {"range": item},
                )
                cls.__children_classes[item] = res
                return res
        return super().__class_getitem__(item)


class PackedArray(TypeBoundPackedArray[T]):
    range: ClassVar[Range]


# class GenericPackedArray(Generic[T], metaclass=PackedArrayMetaclass):

#     def __class_getitem__(cls: Type[_Self], elem_type:) -> "PackedArray[Type[_Self]]":
#         return PackedArrayMetaclass.__new__()


# class PackedStructMetaclass(type(Generic)):
#     ...


# class PackedStruct(metaclass=PackedStructMetaclass):

#     def __class_getitem__(cls: Type[_Self], item: Union[int, slice]) -> PackedArray[Type[_Self]]:
#         ...

#     def __getattribute__(self, attr: str) -> Any:
#         ...

#     def __setattr__(self, attr: str, value: Union[int, str, LogicArray, PackedArray, "PackedStruct"]) -> None:
#         ...


def test_packed_array_subclass_test():
    assert issubclass(GenericPackedArray[int], GenericPackedArray)
    assert issubclass(GenericPackedArray[int][3:1], GenericPackedArray[int])
    assert issubclass(GenericPackedArray[int][3:1], GenericPackedArray)

    class A(GenericPackedArray[int]): ...

    assert issubclass(A, GenericPackedArray)
    assert issubclass(A, GenericPackedArray[int])


def test_unique_identity():
    assert GenericPackedArray[int] is GenericPackedArray[int]
    assert GenericPackedArray[int] is not GenericPackedArray
    assert GenericPackedArray[float] is not GenericPackedArray[int]

    assert GenericPackedArray[int][3:1] is GenericPackedArray[int][3:1]
    assert GenericPackedArray[int][3:1] is not GenericPackedArray[int]
    assert GenericPackedArray[int][3:1] is not GenericPackedArray
    assert GenericPackedArray[int][3:1] is not GenericPackedArray[int][1:3]
