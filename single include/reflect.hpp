#ifndef REFLECT_H
#define REFLECT_H

#include <string>
#include <vector>
#include <map>
#include <type_traits>
#include <tuple>
#include "meta_any.hpp"

namespace Reflect
{

	class TypeDescriptor;

	namespace Details
	{
		
		template <typename T>
		using RawType = typename std::remove_cv<typename std::remove_reference<typename std::remove_pointer<T>::type>::type>::type;

		template <typename Type>
		TypeDescriptor &GetTypeDescriptor()
		{
			static TypeDescriptor typeDescriptor;

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

		template <typename Type>
		TypeDescriptor *Resolve(Type &&object)
		{
			if (!GetTypeDescriptorPtr<RawType<Type>>())
				(void)Resolve<Type>();

			return GetTypeDescriptorPtr<RawType<Type>>();
		}

		inline TypeDescriptor *Resolve(const std::string &name)
		{
			if (auto it = GetTypeRegistry().find(name); it != GetTypeRegistry().end())
				return it->second;

			return nullptr;
		}


	}     // namespace Details

	bool CanCastOrConvert(const TypeDescriptor *from, const TypeDescriptor *to);


	/******** reflected constructor ********/
	class Constructor
	{
	public:
		Any NewInstance(std::vector<Any> &args)
		{
			if (args.size() == mParamTypes.size())
				return NewInstanceImpl(args);

			return Any();
		}

		template <typename... Args>
		Any NewInstance(Args&&... args) const
		{
			if (sizeof...(Args) == mParamTypes.size())
			{
				auto argsAny = std::vector<Any>({ Any(std::forward<Args>(args))... });
				return NewInstanceImpl(argsAny);
			}

			return Any();
		}

		TypeDescriptor const *GetParent() const 
		{ 
			return mParent; 
		}

		TypeDescriptor const *GetParamType(size_t index) const 
		{
			return mParamTypes[index];
		}

		size_t GetNumParams() const 
		{ 
			return mParamTypes.size(); 
		}

		template <typename... Args, size_t... indices>
		bool CanConstruct(std::index_sequence<indices...> indexSequence = std::index_sequence_for<Args...>()) const
		{
			return GetNumParams() == sizeof...(Args) && ((Reflect::CanCastOrConvert(Details::Resolve<Args>(), GetParamType(indices))) && ...);
		}

	protected:
		Constructor(TypeDescriptor *parent, const std::vector<const TypeDescriptor*> &paramTypes) : mParent(parent), mParamTypes(paramTypes) {}

	private:
		virtual Any NewInstanceImpl(std::vector<Any> &args) const = 0;

		TypeDescriptor *mParent;
		std::vector<TypeDescriptor const*> mParamTypes;
	};

	template <typename Type, typename... Args>
	class ConstructorImpl : public Constructor
	{
	public:
		ConstructorImpl() : Constructor(Details::Resolve<Type>(), { Details::Resolve<Args>().../*Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()...*/ }) {}

	private:
		Any NewInstanceImpl(std::vector<Any> &args) const override
		{
			return NewInstanceImpl(args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any NewInstanceImpl(std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if ((std::get<indices>(argsTuple) && ...))
				return Type(*std::get<indices>(argsTuple)...);

			return Any();
		}
	};

	template <typename Type, typename... Args>
	class FreeFunConstructor : public Constructor
	{
	private:
		typedef Type(*CtorFun)(Args...);
	public:
		FreeFunConstructor(CtorFun ctorFun) : Constructor(Details::Resolve<Details::RawType<Type>>(), { Details::Resolve<Details::RawType<Args>>()... }), mCtorFun(ctorFun) {}
	
	private:
		Any NewInstanceImpl(std::vector<Any> &args) const override
		{
			return NewInstanceImpl(args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any NewInstanceImpl(std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if ((std::get<indices>(argsTuple) && ...))
				return mCtorFun(*std::get<indices>(argsTuple)...);

			return Any();
		}
		
		CtorFun mCtorFun;
	};


	/******** reflected data member ********/
	class DataMember
	{
	public:
		std::string GetName() const { return mName; }
		const TypeDescriptor *GetParent() const { return mParent; }
		const TypeDescriptor *GetType() const { return mType; }

		virtual void Set(AnyRef objectHandle, const Any value) = 0; 
		virtual Any Get(Any object) = 0;                                

	protected:
		DataMember(const std::string &name, const TypeDescriptor *type, const TypeDescriptor *parent)
			: mName(name), mType(type), mParent(parent) {}

	private:
		std::string mName;
		const TypeDescriptor *mType;
		const TypeDescriptor *mParent;
	};

	template <typename Class, typename Type>
	class RawDataMember : public DataMember
	{
	public:
		RawDataMember(Type Class::*dataMemberPtr, const std::string name)
			: DataMember(name, Details::Resolve<Type>(), Details::Resolve<Class>()), mDataMemberPtr(dataMemberPtr) {}

		// void Set(AnyRef object, const Any value) override
		// {
		// 	SetImpl(object, value);  // use SFINAE
		// }

		void Set(AnyRef objectHandle, const Any value) override
		{
			SetImpl(objectHandle, value, std::is_const<Type>());  // use tag dispatch
		}

		Any Get(Any object) override
		{
			Class *obj = object.TryCast<Class>();

			if (!obj)
				throw BadCastException(Resolve<Class>()->GetName(), object.GetType()->GetName());

			return obj->*mDataMemberPtr;
		}

	private:
		Type Class::*mDataMemberPtr;

		// ////// use SFINAE
		// template <typename U = Type, typename = typename std::enable_if<!std::is_const<U>::value>::type>
		// void SetImpl(Any object, const Any value)
		// {
		// 	Class *obj = object.TryCast<Class>();
		// 	Any val = value.TryConvert<Type>();
		
		// 	if (!obj)
		// 		throw BadCastException(Resolve<Type>()->GetName(), object.GetType()->GetName(), "object:");
		
		// 	if (!val)
		// 		throw BadCastException(Resolve<Type>()->GetName(), value.GetType()->GetName(), "value:");
		
		// 	obj->*mDataMemberPtr = val.TryCast<Type>();
		// }

		// template <typename U = Type, typename = typename std::enable_if<std::is_const<U>::value>::type, typename = void>
		// void SetImpl(Any object, const Any value)
		// {
		// 	static_assert(false, "can't set const data member");
		// }

		////// use tag dispatch
		void SetImpl(Any object, const Any value, std::false_type)
		{
			Class *obj = object.TryCast<Class>();  // pointers to members of base class can be used with derived class

			Type const *casted = nullptr;
			Any val;
			if (casted = value.TryCast<Type>(); !casted)
			{
				val = value.TryConvert<Type>();
				casted = val.TryCast<Type>();
			}

			if (!obj)
				throw BadCastException(Details::Resolve<Class>()->GetName(), object.GetType()->GetName(), "object:");

			if (!casted)
				throw BadCastException(Details::Resolve<Type>()->GetName(), value.GetType()->GetName(), "value:");

			obj->*mDataMemberPtr = *casted;
		}

		void SetImpl(Any object, const Any value, std::true_type)
		{
			//static_assert(false, "can't set const data member");
		}
	};

	// helper meta function to get info about functions passed as auto non type params
	template <typename>
	struct FunctionHelper;

	template <typename Ret, typename... Args>
	struct FunctionHelper<Ret(Args...)>
	{
		using ReturnType = Ret;
		using ParamsTypes = std::tuple<Args...>;
	};

	template <typename Class, typename Ret, typename... Args>
	/*constexpr*/ FunctionHelper<Ret(/*Class, */Args...)> ToFunctionHelper(Ret(Class::*)(Args...));

	template <typename Class, typename Ret, typename... Args>
	/*constexpr*/ FunctionHelper<Ret(/*Class, */Args...)> ToFunctionHelper(Ret(Class::*)(Args...) const);

	template <typename Ret, typename... Args>
	/*constexpr*/ FunctionHelper<Ret(Args...)> ToFunctionHelper(Ret(*)(Args...));

	template <auto Setter, auto Getter, typename Class> 
	class SetGetDataMember : public DataMember
	{
	private:
		using MemberType = typename decltype(ToFunctionHelper(Getter))::ReturnType;

	public:
		SetGetDataMember(const std::string name)
			: DataMember(name, Details::Resolve<MemberType>(), Details::Resolve<Class>()) {}

		void Set(AnyRef objectHandle, const Any value) override
		{
			Any a = objectHandle;
			Class *obj = a.TryCast<Class>();

			MemberType const *casted = nullptr;
			Any val;
			if (casted = value.TryCast<MemberType>(); !casted)
			{
				val = value.TryConvert<MemberType>();
				casted = val.TryCast<MemberType>();
			}

			if (!obj)
				throw BadCastException(Resolve<Class>()->GetName(), Any(objectHandle).GetType()->GetName(), "object:");

			if (!casted)
				throw BadCastException(Resolve<MemberType>()->GetName(), value.GetType()->GetName(), "value:");

			if constexpr (std::is_member_function_pointer_v<decltype(Setter)>)
				(obj->*Setter)(*casted);
			else
				Setter(*obj, *casted);
		}

		Any Get(Any object) override
		{
			Class *obj = object.TryCast<Class>();

			if (!obj)
				throw BadCastException(Resolve<Class>()->GetName(), object.GetType()->GetName());

			if constexpr (std::is_member_function_pointer_v<decltype(Setter)>)
				return (obj->*Getter)();	
			else
			{
				static_assert(std::is_function_v<std::remove_pointer_t<decltype(Getter)>>);

				return Getter(*obj);
			}
		}
	};


	/******** reflected member function ********/
	class Function
	{
	public:
		std::string GetName() const { return mName; }
		const TypeDescriptor *GetParent() const { return mParent; }

		template <typename... Args>
		Any Invoke(AnyRef object, Args&&... args) const
		{
			if (sizeof...(Args) == mParamTypes.size())
			{
				std::vector<Any> anyArgs{ Any(std::forward<Args>(args))... };

				return InvokeImpl(object, anyArgs);
			}

			return Any();
		}

		const TypeDescriptor *GetReturnType() const
		{
			return mReturnType;
		}

		std::vector<const TypeDescriptor*> GetParamTypes() const
		{
			return mParamTypes;
		}

		const TypeDescriptor *GetParamType(size_t index) const
		{
			return mParamTypes[index];
		}

		std::size_t GetNumParams() const
		{
			return mParamTypes.size();
		}

	protected:
		Function(const std::string &name, const TypeDescriptor *parent, const TypeDescriptor *returnType, const std::vector<TypeDescriptor const*> paramTypes)
			: mName(name), mParent(parent), mReturnType(returnType), mParamTypes(paramTypes) {}

		const TypeDescriptor *mReturnType;
		std::vector<TypeDescriptor const *> mParamTypes;

	private:
		virtual Any InvokeImpl(Any object, std::vector<Any> &args) const = 0;

		std::string mName;
		TypeDescriptor const *const mParent;
	};


	template <typename Ret, typename... Args>
	class FreeFunction : public Function
	{
	private:
		using FunPtr = Ret(*)(Args...);

	public:
		FreeFunction(FunPtr freeFunPtr, const std::string &name)
			: Function(name, nullptr, Details::Resolve<Ret>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }), mFreeFunPtr(freeFunPtr) {}

	private:
		Any InvokeImpl(Any, std::vector<Any> &args) const override
		{
			return InvokeImpl(args, std::index_sequence_for<Args...>());
		}

		template <size_t... indices>
		Any InvokeImpl(std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if ((std::get<indices>(argsTuple) && ...))  // all arguments are valid
				if constexpr (std::is_reference_v<Ret>)
					return AnyRef(mFreeFunPtr(*std::get<indices>(argsTuple)...));
				else
					return mFreeFunPtr(*std::get<indices>(argsTuple)...);
			else
				return Any();
		}

		FunPtr mFreeFunPtr;
	};


	template <typename... Args>
	class FreeFunction<void, Args...> : public Function
	{
	private:
		using FunPtr = void(*)(Args...);

	public:
		FreeFunction(FunPtr freeFunPtr, const std::string &name)
			: Function(name, nullptr, Details::Resolve<void>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }),
			mFreeFunPtr(freeFunPtr) {}

	private:
		Any InvokeImpl(Any, std::vector<Any> &args) const override
		{
			return InvokeImpl(args, std::index_sequence_for<Args...>());
		}

		template <size_t... indices>
		Any InvokeImpl(std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if ((std::get<indices>(argsTuple) && ...))  // all arguments are valid
				mFreeFunPtr(*std::get<indices>(argsTuple)...);

			return Any();
		}

		FunPtr mFreeFunPtr;
	};


	template <typename C, typename Ret, typename... Args>
	class MemberFunction : public Function
	{
	private:
		using MemFunPtr = Ret(C::*)(Args...);

	public:
		MemberFunction(MemFunPtr memFun, const std::string &name)
			: Function(name, Details::Resolve<C>(), Details::Resolve<Ret>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }), mMemFunPtr(memFun) {}

	private:
		Any InvokeImpl(Any object, std::vector<Any> &args) const override
		{
			return InvokeImpl(object, args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any InvokeImpl(Any object, std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if (C *obj = object.TryCast<C>(); (std::get<indices>(argsTuple) && ...) && obj)  // object is valid and all arguments are valid 
				if constexpr (std::is_reference_v<Ret>)
					return AnyRef((obj->*mMemFunPtr)(*std::get<indices>(argsTuple)...));
				else
					return (obj->*mMemFunPtr)(*std::get<indices>(argsTuple)...);
			else
				return Any();
		}

		MemFunPtr mMemFunPtr;
	};


	template <typename C, typename... Args>
	class MemberFunction<C, void, Args...> : public Function
	{
	private:
		using MemFunPtr = void(C::*)(Args...);

	public:
		MemberFunction(MemFunPtr memFun, const std::string &name)
			: Function(name, Details::Resolve<C>(), Details::Resolve<void>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }),
			mMemFunPtr(memFun) {}

	private:
		Any InvokeImpl(Any object, std::vector<Any> &args) const override
		{
			return InvokeImpl(object, args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any InvokeImpl(Any object, std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if (C *obj = object.TryCast<C>(); (std::get<indices>(argsTuple) && ...) && obj)  // object is valid and all arguments are valid 
				(obj->*mMemFunPtr)(*std::get<indices>(argsTuple)...);

			return Any();
		}

		MemFunPtr mMemFunPtr;
	};


	template <typename C, typename Ret, typename... Args>
	class ConstMemberFunction : public Function
	{
	private:
		using ConstMemFunPtr = Ret(C::*)(Args...) const;

	public:
		ConstMemberFunction(ConstMemFunPtr constMemFun, const std::string &name)
			: Function(name, Details::Resolve<C>(), Details::Resolve<Ret>(), { Details::Resolve<std::remove_cv_t<std::remove_reference_t<Args>>>()... }), mConstMemFunPtr(constMemFun) {}

	private:
		Any InvokeImpl(Any object, std::vector<Any> &args) const override
		{
			return InvokeImpl(object, args, std::make_index_sequence<sizeof...(Args)>());
		}

		template <size_t... indices>
		Any InvokeImpl(Any object, std::vector<Any> &args, std::index_sequence<indices...> indexSequence) const
		{
			std::tuple argsTuple = std::make_tuple(args[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);
			std::vector<Any> convertedArgs{ (std::get<indices>(argsTuple) ? AnyRef(*std::get<indices>(argsTuple)) : args[indices].TryConvert<std::remove_cv_t<std::remove_reference_t<Args>>>())... };
			argsTuple = std::make_tuple(convertedArgs[indices].TryCast<std::remove_cv_t<std::remove_reference_t<Args>>>()...);

			if (C *obj = object.TryCast<C>(); obj && (std::get<indices>(argsTuple) && ...))  // object is valid and all arguments are valid 
				if constexpr (std::is_void<Ret>::value)
				{
					(obj->*mConstMemFunPtr)(*std::get<indices>(argsTuple)...);

					return Any();
				}
				else
					if constexpr (std::is_reference_v<Ret>)
						return AnyRef((obj->*mConstMemFunPtr)(*std::get<indices>(argsTuple)...));
					else
						return (obj->*mConstMemFunPtr)(*std::get<indices>(argsTuple)...);
			else
				return Any();
		}

		ConstMemFunPtr mConstMemFunPtr;
	};


	/******** reflected base class ********/
	class Base
	{
	public:
		const TypeDescriptor *GetType() const { return mType; }

		virtual void *Cast(void *object) = 0;

	protected:
		Base(TypeDescriptor const *type, const TypeDescriptor *parent)
			: mParent(parent), mType(type) {}

	private:
		const TypeDescriptor *mParent;
		const TypeDescriptor *mType;
	};

	template <typename B, typename D>
	class BaseImpl : public Base
	{
	public:
		BaseImpl() : Base(Details::Resolve<B>(), Details::Resolve<D>()) {}

		void *Cast(void *object) override
		{
			return static_cast<B*>(object);
		}
	};


	/******** reflected conversion function ********/
	class Conversion
	{
	public:
		const TypeDescriptor *GetFromType() const { return mFromType; }
		const TypeDescriptor *GetToType() const { return mToType; }

		virtual Any Convert(const void *object) const = 0;

	protected:
		Conversion(const TypeDescriptor *from, const TypeDescriptor *to)
			: mFromType(from), mToType(to) {}

	private:
		const TypeDescriptor *mFromType;  // type to convert from
		const TypeDescriptor *mToType;    // type to convert to
	};

	template <typename From, typename To>
	class ConversionImpl : public Conversion
	{
	public:
		ConversionImpl() : Conversion(Resolve<From>(), Resolve<To>()) {}

		Any Convert(const void *object) const override
		{
			//return To(*static_cast<const From*>(object));
			return static_cast<To>(*static_cast<const From*>(object));
		}
	};


	/******** type descriptor ********/
	class TypeDescriptor
	{
		template <typename> friend class TypeFactory;
		template <typename> friend TypeDescriptor *Details::Resolve();
		template <typename T> friend TypeDescriptor *Details::Resolve(T&&);
	public:
		template <typename Type, typename... Args>
		void AddConstructor()
		{
			Constructor *constructor = new ConstructorImpl<Type, Args...>();

			mConstructors.push_back(constructor);
		}

		template <typename Type, typename... Args>
		void AddConstructor(Type(*ctorFun)(Args...))
		{
			Constructor *constructor = new FreeFunConstructor<Type, Args...>(ctorFun);

			mConstructors.push_back(constructor);
		}

		template <typename B, typename T>
		void AddBase()
		{
			Base *base = new BaseImpl<B, T>;

			mBases.push_back(base);
		}

		template <typename C, typename T>
		void AddDataMember(T C::*dataMemPtr, const std::string &name)
		{
			DataMember *dataMember = new RawDataMember<C, T>(dataMemPtr, name);

			mDataMembers.push_back(dataMember);
		}

		template <auto Setter, auto Getter, typename Type>
		void AddDataMember(const std::string &name)
		{
			DataMember *dataMember = new SetGetDataMember<Setter, Getter, Type>(name);

			mDataMembers.push_back(dataMember);
		}

		template <typename Ret, typename... Args>
		void AddMemberFunction(Ret freeFun(Args...), const std::string &name)
		{
			Function *memberFunction = new FreeFunction<Ret, Args...>(freeFun, name);

			mMemberFunctions.push_back(memberFunction);
		}

		template <typename C, typename Ret, typename... Args>
		void AddMemberFunction(Ret(C::*memFun)(Args...), const std::string &name)
			{
				Function *memberFunction = new MemberFunction<C, Ret, Args...>(memFun, name);
	
				mMemberFunctions.push_back(memberFunction);
		}

		template <typename C, typename Ret, typename... Args>
		void AddMemberFunction(Ret(C::*memFun)(Args...) const, const std::string &name)
		{
			Function *memberFunction = new ConstMemberFunction<C, Ret, Args...>(memFun, name);

			mMemberFunctions.push_back(memberFunction);
		}

		template <typename From, typename To>
		void AddConversion()
		{
			Conversion *conversion = new ConversionImpl<From, To>;

			mConversions.push_back(conversion);
		}
		
		std::string const &GetName() const { return mName; }

		std::size_t GetSize() const { return mSize; }

		std::vector<Constructor*> GetConstructors() const { return mConstructors; }

		template <typename... Args>
		const Constructor *GetConstructor() const
		{
			for (auto *constructor : mConstructors)
				if (constructor->CanConstruct<Args...>(std::index_sequence_for<Args...>()))
					//if (constructor->CanConstruct<Args...>(std::make_index_sequence<sizeof...(Args)>()))
					return constructor;

			return nullptr;
		}

		std::vector<Base*> GetBases() const { return mBases; }

		template <typename B>
		Base *GetBase() const
		{
			for (auto base : mBases)
				if (base->GetType() == Resolve<B>)
					return base;

			return nullptr;
		}

		std::vector<DataMember*> GetDataMembers() const
		{
			std::vector<DataMember*> dataMembers(mDataMembers);

			for (auto *base : mBases)
				for (auto dataMember : base->GetType()->GetDataMembers())
					dataMembers.push_back(dataMember);

			return dataMembers;
		}

		DataMember *GetDataMember(const std::string &name) const
		{
			for (auto *dataMember : mDataMembers)
				if (dataMember->GetName() == name)
					return dataMember;

			for (auto *base : mBases)
				if (auto *baseDataMember = base->GetType()->GetDataMember(name))
					return baseDataMember;

			return nullptr;
		}

		std::vector<Function*> GetMemberFunctions() const
		{
			std::vector<Function*> memberFunctions(mMemberFunctions);

			for (auto *base : mBases)
				for (auto memberFunction : base->GetType()->GetMemberFunctions())
					memberFunctions.push_back(memberFunction);

			return memberFunctions;
		}

		const Function *GetMemberFunction(const std::string &name) const
		{
			for (auto *memberFunction : mMemberFunctions)
				if (memberFunction->GetName() == name)
					return memberFunction;

			for (auto *base : mBases)
				if (auto *memberFunction = base->GetType()->GetMemberFunction(name))
					return memberFunction;

			return nullptr;
		}

		std::vector<Conversion*> GetConversions() const { return mConversions; }

		template <typename To>
		Conversion *GetConversion() const
		{
			for (auto conversion : mConversions)
				if (conversion->GetToType() == Resolve<To>())
					return conversion;

			return nullptr;
		}

	private:
		std::string mName;
		std::size_t mSize;

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

		std::vector<Base*> mBases;
		std::vector<Conversion*> mConversions;
		std::vector<Constructor*> mConstructors;
		std::vector<DataMember*> mDataMembers;
		std::vector<Function*> mMemberFunctions;

	};

	inline bool CanCastOrConvert(const TypeDescriptor *from, const TypeDescriptor *to) 
	{
		if (from == to)
			return true;

		for (auto *base : from->GetBases())
			if (base->GetType() == to)
				return true;

		for (auto *conversion : from->GetConversions())
			if (conversion->GetToType() == to)
				return true;

		return false;
	}

	/******** type factory ********/

	// fwd declaration
	template <typename Type>
	class TypeFactory;

	// variable template (one type factory per type)
	template <typename Type>
	TypeFactory<Type> typeFactory;

	template <typename Type>
	class TypeFactory
	{
	public:
		TypeFactory &ReflectType(const std::string &name)
		{
			TypeDescriptor *typeDescriptor = Details::Resolve<Type>();
			
			typeDescriptor->mName = name;
			Details::GetTypeRegistry()[name] = typeDescriptor;

			return typeFactory<Type>;
		}

		template <typename... Args>
		TypeFactory &AddConstructor()
		{
			Details::Resolve<Type>()->template AddConstructor<Type, Args...>();

			return typeFactory<Type>;
		}

		template <typename... Args>
		TypeFactory &AddConstructor(Type (*ctorFun)(Args...))
		{
			Details::Resolve<Type>()->template AddConstructor<Type, Args...>(ctorFun);

			return *this;
		}

		template <typename Base>
		TypeFactory &AddBase()
		{
			static_assert(std::is_base_of<Base, Type>::value);  // Base must be a base of Type

			Details::Resolve<Type>()->template AddBase<Base, Type>();

			return typeFactory<Type>;
		}

		template <typename T, typename U = Type>  // default template type param to allow for non class types
		TypeFactory &AddDataMember(T U::*dataMemPtr, const std::string &name)
		{
			Details::Resolve<Type>()->AddDataMember(dataMemPtr, name);

			return typeFactory<Type>;
		}

		template <auto Setter, auto Getter>
		TypeFactory &AddDataMember(const std::string &name)
		{
			Details::Resolve<Type>()->template AddDataMember<Setter, Getter, Type>(name);

			return typeFactory<Type>;
		}

		template <typename Ret, typename... Args>
		TypeFactory &AddMemberFunction(Ret(*freeFun)(Args...), const std::string &name)
		{
			Details::Resolve<Type>()->AddMemberFunction(freeFun, name);

			return typeFactory<Type>;
		}

		template <typename Ret, typename... Args, typename U = Type>
		TypeFactory &AddMemberFunction(Ret(U::*memFun)(Args...), const std::string &name)
		{
			Details::Resolve<Type>()->AddMemberFunction(memFun, name);

			return typeFactory<Type>;
		}

		template <typename Ret, typename... Args, typename U = Type>
		TypeFactory &AddMemberFunction(Ret(U::*constMemFun)(Args...) const, const std::string &name)
		{
			Details::Resolve<Type>()->AddMemberFunction(constMemFun, name);

			return typeFactory<Type>;
		}

		//template <typename FuncType, FuncType Func>
		//template <auto Func>
		//TypeFactory &AddMemberFunction(const std::string &name)
		//{
		//	Details::Resolve<Type>()->template AddMemberFunction<FunctType, Func>(name);
		//	Details::Resolve<Type>()->template AddMemberFunction<Func>(name);

		//	return typeFactory<Type>;
		//}

		template <typename To>
		TypeFactory &AddConversion()
		{
			static_assert(std::is_convertible_v<Type, To>);  // a conversion Type -> To must exist

			Details::Resolve<Type>()->template AddConversion<Type, To>();

			return typeFactory<Type>;
		}
	};


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
	* returns a const pointer to the type descriptor
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
		return Details::Resolve(name);
	}

}    // namespace Reflect

#endif  // REFLECT_H