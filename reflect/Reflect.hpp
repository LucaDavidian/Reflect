#ifndef REFLECT_H
#define REFLECT_H

#include "TypeFactory.hpp"
#include <type_traits>
#include <utility>

namespace Reflect
{

	class TypeDescriptor;

	/*
	* Reflect takes a string which is the mapped name of the
	* reflected type and returns a TypeFactory, which can be
	* used to add meta objects to the type descriptor
	*/

	template <typename Type>
	TypeFactory<Type> &Reflect(const std::string &name)
	{
		return typeFactory<Type>.ReflectType(name);
	}

	/*
	* three ways to get the type descriptor of a type:
	* 1. with a template type parameter
	* 2. with the name of the type (a string)
	* 3. with an instance of the object
	*
	* each function calls the corresponding internal Resolve and returns a const pointer to the type descriptor
	*/

	template <typename Type>
	const TypeDescriptor *Resolve()
	{
		return Details::Resolve<Type>();
	}

	template <typename T, typename = typename std::enable_if<!std::is_convertible<T, std::string>::value>::type>
	const TypeDescriptor *Resolve(T &&object)
	{
		return Details::Resolve(std::forward<T>(object));
	}
	
	inline const TypeDescriptor *Resolve(const std::string &name)
	{
		if (auto it = Details::GetTypeRegistry().find(name); it != Details::GetTypeRegistry().end())
			return it->second;

		return nullptr;
	}

}  // namespace Reflect

#endif  // REFLECT_H