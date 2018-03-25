
#ifndef SRC_BASE_TYPESMACROS_H_
#define SRC_BASE_TYPESMACROS_H_
#define buildForAllTypes(f, sep) \
    f(Bool) sep f(Int) sep f(Set) sep f(MSet) sep f(Tuple) sep f(Sequence)

#define MACRO_COMMA ,

#endif /* SRC_BASE_TYPESMACROS_H_ */
