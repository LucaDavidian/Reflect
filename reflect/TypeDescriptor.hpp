#ifndef TYPE_DESCRIPTOR_H
#define TYPE_DESCRIPTOR_H

#include <string>
#include <vector>
#include <map>
#include <type_traits>

namespace Reflect
{

	// fwd declarations
	class DataMember;
	class Function;
	class Constructor;
	class Base;
	class Conversion;
	
	template <typename>
	class TypeFactory;

	class TypeDescriptor;

	// fwd declarations (for friend declarations inside TypeDescriptor)
	namespace Details
	{

		template <typename Type>
		TypeDescriptor *Resolve();

		template <typename Type>
		TypeDescriptor *Resolve(Type &&);

	}	// namespace Details

	class TypeDescriptor
	{
		template <typename> friend class TypeFactory;

		template <typename Type> friend TypeDescriptor *Details::Resolve();
		template <typename Type> friend TypeDescriptor *Details::Resolve(Type &&);

	public:
		template <typename Type, typename... Args>
		void AddConstructor();

		template <typename Type, typename... Args>
		void AddConstructor(Type (*)(Args...));

		template <typename B, typename T>
		void AddBase();

		template <typename C, typename T>
		void AddDataMember(T C::*dataMemPtr, const std::string &name);

		template <auto Setter, auto Getter, typename Type>
		void AddDataMember(const std::string &name);

		template <typename Ret, typename... Args>
		void AddMemberFunction(Ret freeFun(Args...), const std::string &name);

		template <typename C, typename Ret, typename... Args>
		void AddMemberFunction(Ret(C::*memFun)(Args...), const std::string &name);

		template <typename C, typename Ret, typename... Args>
		void AddMemberFunction(Ret(C::*memFun)(Args...) const, const std::string &name);

		template <typename From, typename To>
		void AddConversion();

		std::string const &GetName() const;

		//std::size_t GetSize() const;

		std::vector<Constructor*> GetConstructors() const;

		template <typename... Args>
		const Constructor *GetConstructor() const;

		std::vector<Base*> GetBases() const;

		template <typename B>
		Base *GetBase() const;

		std::vector<DataMember*> GetDataMembers() const;

		DataMember *GetDataMember(const std::string &name) const;

		std::vector<Function*> GetMemberFunctions() const;

		const Function *GetMemberFunction(const std::string &name) const;

		std::vector<Conversion*> GetConversions() const;

		template <typename To>
		Conversion *GetConversion() const;

	private:
		std::string mName;
		std::size_t mSize;

		std::vector<Base*> mBases;
		std::vector<Conversion*> mConversions;
		std::vector<Constructor*> mConstructors;
		std::vector<DataMember*> mDataMembers;
		std::vector<Function*> mMemberFunctions;

		// C++ primary type categories
		bool mIsVoid;
		bool mIsIntegral;
		bool mIsFloatingPoint;
		bool mIsArray;
		bool mIsPointer;
		bool mIsPointerToDataMember;
		bool mIsPointerToMemberFunction;
		bool mIsNullPointer;
		//bool mIsLValueReference;
		//bool mIsRValueReference;
		bool mIsClass;
		bool mIsUnion;
		bool mIsEnum;
		bool mIsFunction;
	};

	namespace Details
	{
		/* 
		* all cv and reference qualifiers are stripped, but pointers are distinct types
		* (i.e. int and int* have two distint type descriptors)
		*/
		template <typename T>
		using RawType = typename std::remove_cv<std::remove_reference_t<T>>::type;   

		template <typename T>
		TypeDescriptor &GetTypeDescriptor()
		{
			static TypeDescriptor typeDescriptor;  // single instance of type descriptor per reflected type

			return typeDescriptor;
		}

		template <typename Type>
		TypeDescriptor *&GetTypeDescriptorPtr()
		{
			static TypeDescriptor *typeDescriptorPtr = nullptr;  // single instance of type descriptor ptr per reflected type

			return typeDescriptorPtr;
		}

		inline auto &GetTypeRegistry()
		{
			static std::map<std::string, TypeDescriptor*> typeRegistry;

			return typeRegistry;
		}

		template <typename Type>
		inline constexpr auto GetTypeSize() -> typename std::enable_if<!std::is_same<RawType<Type>, void>::value, std::size_t>::type
		{
			return sizeof(Type);
		}

		template <typename Type>
		inline constexpr auto GetTypeSize() -> typename std::enable_if<std::is_same<RawType<Type>, void>::value, std::size_t>::type
		{
			return 0U;
		}

		// internal function template that returns a type descriptor by type
		template <typename Type>
		TypeDescriptor *Resolve()
		{
			TypeDescriptor *&typeDescPtr = GetTypeDescriptorPtr<RawType<Type>>();

			if (!typeDescPtr)  // create a type descriptor if not present
			{
				TypeDescriptor &typeDesc = GetTypeDescriptor<RawType<Type>>();			
				typeDescPtr = &typeDesc;  

				typeDesc.mSize = GetTypeSize<Type>();

				typeDesc.mIsVoid = std::is_void_v<Type>;
				typeDesc.mIsIntegral = std::is_integral_v<Type>;
				typeDesc.mIsFloatingPoint = std::is_floating_point_v<Type>;
				typeDesc.mIsArray = std::is_array_v<Type>;
				typeDesc.mIsPointer = std::is_pointer_v<Type>;
				typeDesc.mIsPointerToDataMember = std::is_member_object_pointer_v<Type>;
				typeDesc.mIsPointerToMemberFunction = std::is_member_function_pointer_v<Type>;
				typeDesc.mIsNullPointer = std::is_null_pointer_v<Type>;
				//typeDesc.mIsLValueReference = std::is_lvalue_reference_v<Type>;
				//typeDesc.mIsRValueReference = std::is_rvalue_reference_v<Type>;
				typeDesc.mIsClass = std::is_class_v<std::remove_pointer_t<Type>>;
				typeDesc.mIsUnion = std::is_union_v<Type>;
				typeDesc.mIsEnum = std::is_enum_v<Type>;
				typeDesc.mIsFunction = std::is_function_v<Type>;
			}

			return typeDescPtr;
		}

		// internal function template that returns a type descriptor by object
		template <typename Type>
		TypeDescriptor *Resolve(Type &&object)
		{
			if (!GetTypeDescriptorPtr<RawType<Type>>())
				Resolve<Type>();

			return GetTypeDescriptorPtr<RawType<Type>>();
		}

	}  // namespace Details

}  // namespace Reflect

#include "DataMember.hpp"
#include "Function.hpp"
#include "Constructor.hpp"
#include "Base.hpp"
#include "Conversion.hpp"

#include "TypeDescriptor.inl"

#endif // TYPE_DESCRIPTOR_H