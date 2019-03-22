#pragma once

namespace xlang
{
    using namespace meta::reader;

    inline auto get_start_time()
    {
        return std::chrono::high_resolution_clock::now();
    }

    inline auto get_elapsed_time(std::chrono::time_point<std::chrono::high_resolution_clock> const& start)
    {
        return std::chrono::duration_cast<std::chrono::duration<int64_t, std::milli>>(std::chrono::high_resolution_clock::now() - start).count();
    }

    auto get_dotted_name_segments(std::string_view ns)
    {
        std::vector<std::string_view> segments;
        size_t pos = 0;

        while (true)
        {
            auto new_pos = ns.find('.', pos);

            if (new_pos == std::string_view::npos)
            {
                segments.push_back(ns.substr(pos));
                return std::move(segments);
            }

            segments.push_back(ns.substr(pos, new_pos - pos));
            pos = new_pos + 1;
        };
    };

    bool is_exclusive_to(TypeDef const& type)
    {
        return get_category(type) == category::interface_type && get_attribute(type, "Windows.Foundation.Metadata", "ExclusiveToAttribute");
    }

    bool is_flags_enum(TypeDef const& type)
    {
        return get_category(type) == category::enum_type && get_attribute(type, "System", "FlagsAttribute");
    }

    bool is_ptype(TypeDef const& type)
    {
        return distance(type.GenericParam()) > 0;
    }

    bool is_static(TypeDef const& type)
    {
        return get_category(type) == category::class_type && type.Flags().Abstract();
    }

    enum class fundamental_type
    {
        Boolean,
        Char,
        Int8,
        UInt8,
        Int16,
        UInt16,
        Int32,
        UInt32,
        Int64,
        UInt64,
        Float,
        Double,
        String,
    };

    struct generic_type_instance;
    struct object_type {};
    struct guid_type {};
    using type_definition = TypeDef;
    using generic_type_index = GenericTypeIndex;

    using type_semantics = std::variant<
        fundamental_type,
        object_type,
        guid_type,
        type_definition,
        generic_type_instance,
        generic_type_index>;

    struct generic_type_instance
    {
        type_definition generic_type;
        std::vector<type_semantics> generic_args{};
    };

    type_semantics get_type_semantics(TypeSig const& signature);

    type_semantics get_type_semantics(GenericTypeInstSig const& type)
    {
        auto generic_type_helper = [&type]()
        {
            switch (type.GenericType().type())
            {
            case TypeDefOrRef::TypeDef:
                return type.GenericType().TypeDef();
            case TypeDefOrRef::TypeRef:
                return find_required(type.GenericType().TypeRef());
            }

            throw_invalid("invalid TypeDefOrRef value for GenericTypeInstSig.GenericType");
        };

        auto gti = generic_type_instance{ generic_type_helper() };

        for (auto&& arg : type.GenericArgs())
        {
            gti.generic_args.push_back(get_type_semantics(arg));
        }

        return gti;
    }

    type_semantics get_type_semantics(coded_index<TypeDefOrRef> const& type)
    {
        switch (type.type())
        {
        case TypeDefOrRef::TypeDef:
            return type.TypeDef();
        case TypeDefOrRef::TypeRef:
        {
            auto type_ref = type.TypeRef();
            if (type_ref.TypeName() == "Guid" && type_ref.TypeNamespace() == "System")
            {
                return guid_type{};
            }

            return find_required(type_ref);
        }
        case TypeDefOrRef::TypeSpec:
            return get_type_semantics(type.TypeSpec().Signature().GenericTypeInst());
        }

        throw_invalid("TypeDefOrRef not supported");
    }

    namespace impl
    {
        template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
        template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;
    }

    type_semantics get_type_semantics(TypeSig const& signature)
    {
        return std::visit(
            impl::overloaded{
            [](ElementType type) -> type_semantics
        {
            switch (type)
            {
            case ElementType::Boolean:
                return fundamental_type::Boolean;
            case ElementType::Char:
                return fundamental_type::Char;
            case ElementType::I1:
                return fundamental_type::Int8;
            case ElementType::U1:
                return fundamental_type::UInt8;
            case ElementType::I2:
                return fundamental_type::Int16;
            case ElementType::U2:
                return fundamental_type::UInt16;
            case ElementType::I4:
                return fundamental_type::Int32;
            case ElementType::U4:
                return fundamental_type::UInt32;
            case ElementType::I8:
                return fundamental_type::Int64;
            case ElementType::U8:
                return fundamental_type::UInt64;
            case ElementType::R4:
                return fundamental_type::Float;
            case ElementType::R8:
                return fundamental_type::Double;
            case ElementType::String:
                return fundamental_type::String;
            case ElementType::Object:
                return object_type{};
            }

            throw_invalid("element type not supported");
        },
            [](coded_index<TypeDefOrRef> type) -> type_semantics
        {
            return get_type_semantics(type);
        },
            [](GenericTypeIndex var) -> type_semantics { return generic_type_index{ var.index }; },
            [](GenericTypeInstSig sig) -> type_semantics { return get_type_semantics(sig); }
            }, signature.Type());
    }

    struct method_signature
    {
        using param_t = std::pair<Param, ParamSig const*>;

        explicit method_signature(MethodDef const& method) :
            m_method(method.Signature())
        {
            auto params = method.ParamList();

            if (m_method.ReturnType() && params.first != params.second && params.first.Sequence() == 0)
            {
                m_return = params.first;
                ++params.first;
            }

            for (uint32_t i{}; i != m_method.Params().size(); ++i)
            {
                m_params.emplace_back(params.first + i, m_method.Params().data() + i);
            }
        }

        std::vector<param_t>& params()
        {
            return m_params;
        }

        std::vector<param_t> const& params() const
        {
            return m_params;
        }

        auto const& return_signature() const
        {
            return m_method.ReturnType();
        }

        auto return_param_name() const
        {
            std::string_view name;

            if (m_return)
            {
                name = m_return.Name();
            }
            else
            {
                name = "_return_value";
            }

            return name;
        }

        bool has_params() const
        {
            return !m_params.empty();
        }

    private:

        MethodDefSig m_method;
        std::vector<param_t> m_params;
        Param m_return;
    };

    struct interface_info
    {
        TypeDef type;
        std::vector<std::string> type_arguments;
    };

    void push_interface_info(std::vector<interface_info>& interfaces, interface_info&& info)
    {
        auto iter = std::find_if(interfaces.begin(), interfaces.end(), [&info](auto i)
        {
            return i.type == info.type;
        });

        if (iter == interfaces.end())
        {
            interfaces.push_back(std::move(info));
        }
    }

    //void collect_required_interfaces(writer& w, std::vector<interface_info>& sigs, coded_index<TypeDefOrRef> const& index);

    //void collect_required_interfaces(writer& w, std::vector<interface_info>& interfaces, TypeDef const& type)
    //{
    //    if (get_category(type) == category::interface_type)
    //    {
    //        interface_info info;
    //        info.type = type;
    //        for (auto&& gp : type.GenericParam())
    //        {
    //            info.type_arguments.push_back(std::string{ gp.Name() });
    //        }
    //        push_interface_info(interfaces, std::move(info));
    //    }

    //    auto guard{ w.push_generic_params(type.GenericParam()) };
    //    for (auto&& ii : type.InterfaceImpl())
    //    {
    //        collect_required_interfaces(w, interfaces, ii.Interface());
    //    }
    //}

    //void collect_required_interfaces(writer& w, std::vector<interface_info>& interfaces, GenericTypeInstSig const& sig)
    //{
    //    TypeDef type{};
    //    switch (sig.GenericType().type())
    //    {
    //    case TypeDefOrRef::TypeDef:
    //        type = sig.GenericType().TypeDef();
    //        break;
    //    case TypeDefOrRef::TypeRef:
    //        type = find_required(sig.GenericType().TypeRef());
    //        break;
    //    case TypeDefOrRef::TypeSpec:
    //        throw_invalid("collect_required_interfaces");
    //    }

    //    interface_info info;
    //    info.type = type;

    //    for (auto&& gp : sig.GenericArgs())
    //    {
    //        auto q = w.write_temp("%", gp);
    //        info.type_arguments.push_back(q);
    //    }

    //    push_interface_info(interfaces, std::move(info));

    //    auto guard{ w.push_generic_params(sig) };
    //    for (auto&& ii : type.InterfaceImpl())
    //    {
    //        collect_required_interfaces(w, interfaces, ii.Interface());
    //    }
    //}

    //void collect_required_interfaces(writer& w, std::vector<interface_info>& sigs, coded_index<TypeDefOrRef> const& index)
    //{
    //    switch (index.type())
    //    {
    //    case TypeDefOrRef::TypeDef:
    //        collect_required_interfaces(w, sigs, index.TypeDef());
    //        break;
    //    case TypeDefOrRef::TypeRef:
    //        collect_required_interfaces(w, sigs, find_required(index.TypeRef()));
    //        break;
    //    case TypeDefOrRef::TypeSpec:
    //        collect_required_interfaces(w, sigs, index.TypeSpec().Signature().GenericTypeInst());
    //        break;
    //    }
    //}

    //auto get_required_interfaces(TypeDef const& type)
    //{
    //    writer w;
    //    auto guard{ w.push_generic_params(type.GenericParam()) };

    //    std::vector<interface_info> interfaces;
    //    collect_required_interfaces(w, interfaces, type);

    //    return std::move(interfaces);
    //}

    TypeDef get_typedef(type_semantics const& semantics)
    {
        return std::visit(
            impl::overloaded{
                [](type_definition type) { return type; },
                [](generic_type_instance type_instance) { return type_instance.generic_type; },
                [](auto) -> TypeDef { throw_invalid("type doesn't contain typedef"); }
            }, semantics);
    };

    TypeDef get_typedef(coded_index<TypeDefOrRef> const& type)
    {
        return get_typedef(get_type_semantics(type));
    };

    bool implements_interface(TypeDef const& type, std::string_view const& ns, std::string_view const& name)
    {
        auto type_name_matches = [&ns, &name](TypeDef const& td) { return td.TypeNamespace() == ns && td.TypeName() == name; };

        if (get_category(type) == category::interface_type && type_name_matches(type))
            return true;

        for (auto&& ii : type.InterfaceImpl())
        {
            auto interface_type = get_typedef(ii.Interface());
            if (implements_interface(interface_type, ns, name))
                return true;
        }

        return false;
    }

    bool implements_interface(TypeDef const& type, std::vector<std::tuple<std::string_view, std::string_view>> names)
    {
        for (auto&&[ns, name] : names)
        {
            if (implements_interface(type, ns, name))
            {
                return true;
            }
        }

        return false;
    }

    auto get_member_name(MethodDef const& method)
    {
        auto overload_attrib = get_attribute(method, "Windows.Foundation.Metadata", "OverloadAttribute");
        if (overload_attrib)
        {
            auto args = overload_attrib.Value().FixedArgs();
            return std::get<std::string_view>(std::get<ElemSig>(args[0].value).value);
        }
        else
        {
            return method.Name();
        }
    }

    auto get_delegate_invoke(TypeDef const& type)
    {
        XLANG_ASSERT(get_category(type) == category::delegate_type);

        for (auto&& method : type.MethodList())
        {
            if (method.SpecialName() && (method.Name() == "Invoke"))
            {
                return method;
            }
        }

        throw_invalid("Invoke method not found");
    }

    inline bool is_constructor(MethodDef const& method)
    {
        return method.Flags().RTSpecialName() && method.Name() == ".ctor";
    }

    inline bool is_static(MethodDef const& method)
    {
        return method.Flags().Static();
    }

    inline bool is_get_method(MethodDef const& method)
    {
        return method.SpecialName() && starts_with(method.Name(), "get_");
    }

    inline bool is_put_method(MethodDef const& method)
    {
        return method.SpecialName() && starts_with(method.Name(), "put_");
    }

    inline bool is_add_method(MethodDef const& method)
    {
        return method.SpecialName() && starts_with(method.Name(), "add_");
    }

    inline bool is_remove_method(MethodDef const& method)
    {
        return method.SpecialName() && starts_with(method.Name(), "remove_");
    }

    std::string_view get_method_abi_name(MethodDef const& method)
    {
        if (is_constructor(method))
        {
            if (empty(method.ParamList()))
            {
                return "_default_ctor";
            }
            else
            {
                method_signature signature{ method };

                auto type = method.Parent();
                for (auto&& attrib : type.CustomAttribute())
                {
                    auto pair = attrib.TypeNamespaceAndName();
                    if (pair.second == "ActivatableAttribute" && pair.first == "Windows.Foundation.Metadata")
                    {
                        auto fixed_args = attrib.Value().FixedArgs();
                        auto elem0 = std::get<ElemSig>(fixed_args[0].value);

                        // if the first param is SystemType, it holds the name of a factory interface
                        if (std::holds_alternative<ElemSig::SystemType>(elem0.value))
                        {
                            auto factory_type = type.get_cache().find_required(
                                std::get<ElemSig::SystemType>(elem0.value).name);

                            for (auto&& factory_method : factory_type.MethodList())
                            {
                                method_signature factory_signature{ factory_method };
                                if (signature.params().size() == factory_signature.params().size())
                                {
                                    // TODO: compare method param types
                                    return get_method_abi_name(factory_method);
                                }
                            }
                        }
                    }
                }

                std::string msg{ "couldn't find factory method " };
                msg.append(type.TypeNamespace());
                msg.append(".");
                msg.append(type.TypeName());
                throw_invalid(msg);
            }
        }
        else
        {
            auto overload_attrib = get_attribute(method, "Windows.Foundation.Metadata", "OverloadAttribute");
            if (overload_attrib)
            {
                auto args = overload_attrib.Value().FixedArgs();
                return std::get<std::string_view>(std::get<ElemSig>(args[0].value).value);
            }

            return method.Name();
        }
    }

    auto get_property_methods(Property const& prop)
    {
        MethodDef get_method{}, set_method{};

        for (auto&& method_semantic : prop.MethodSemantic())
        {
            auto semantic = method_semantic.Semantic();

            if (semantic.Getter())
            {
                get_method = method_semantic.Method();
            }
            else if (semantic.Setter())
            {
                set_method = method_semantic.Method();
            }
            else
            {
                throw_invalid("Properties can only have get and set methods");
            }
        }

        XLANG_ASSERT(get_method);

        if (set_method)
        {
            XLANG_ASSERT(get_method.Flags().Static() == set_method.Flags().Static());
        }

        return std::make_tuple(get_method, set_method);
    }

    auto get_event_methods(Event const& evt)
    {
        MethodDef add_method{}, remove_method{};

        for (auto&& method_semantic : evt.MethodSemantic())
        {
            auto semantic = method_semantic.Semantic();

            if (semantic.AddOn())
            {
                add_method = method_semantic.Method();
            }
            else if (semantic.RemoveOn())
            {
                remove_method = method_semantic.Method();
            }
            else
            {
                throw_invalid("Events can only have add and remove methods");
            }
        }

        XLANG_ASSERT(add_method);
        XLANG_ASSERT(remove_method);
        XLANG_ASSERT(add_method.Flags().Static() == remove_method.Flags().Static());

        return std::make_tuple(add_method, remove_method);
    }

    enum class param_category
    {
        in,
        out,
        pass_array,
        fill_array,
        receive_array,
    };

    auto get_param_category(method_signature::param_t const& param)
    {
        if (param.second->Type().is_szarray())
        {
            if (param.first.Flags().In())
            {
                return param_category::pass_array;
            }
            else if (param.second->ByRef())
            {
                XLANG_ASSERT(param.first.Flags().Out());
                return param_category::receive_array;
            }
            else
            {
                XLANG_ASSERT(param.first.Flags().Out());
                return param_category::fill_array;
            }
        }
        else
        {
            if (param.first.Flags().In())
            {
                XLANG_ASSERT(!param.first.Flags().Out());
                return param_category::in;
            }
            else
            {
                XLANG_ASSERT(param.first.Flags().Out());
                return param_category::out;
            }
        }
    }

    auto get_param_category(RetTypeSig const& sig)
    {
        if (sig.Type().is_szarray())
        {
            return param_category::receive_array;
        }
        else
        {
            return param_category::out;
        }
    }

    bool is_in_param(method_signature::param_t const& param)
    {
        auto category = get_param_category(param);

        return (category == param_category::in
            || category == param_category::pass_array
            // Note, fill array acts as in and out param in Python
            || category == param_category::fill_array);
    }

    bool is_out_param(method_signature::param_t const& param)
    {
        auto category = get_param_category(param);

        return (category == param_category::out
            || category == param_category::receive_array
            // Note, fill array acts as in and out param in Python
            || category == param_category::fill_array);
    }

    int count_in_param(std::vector<method_signature::param_t> const& params)
    {
        int count{ 0 };

        for (auto&& param : params)
        {
            if (is_in_param(param))
            {
                count++;
            }
        }

        return count;
    }

    enum class argument_convention
    {
        no_args,
        single_arg,
        variable_args
    };

    auto get_argument_convention(MethodDef const& method)
    {
        if (is_constructor(method) && empty(method.ParamList()))
        {
            return argument_convention::no_args;
        }
        else if (is_get_method(method))
        {
            return argument_convention::no_args;
        }
        else if (is_put_method(method) || is_add_method(method) || is_remove_method(method))
        {
            return argument_convention::single_arg;
        }
        else
        {
            return argument_convention::variable_args;
        }
    }

    type_semantics get_struct_field_semantics(Field const& field, bool convert_enum_to_underlying)
    {
        return std::visit(impl::overloaded
        {
            [&](type_definition const& type) -> type_semantics
            {
                auto category = get_category(type);
                XLANG_ASSERT(category == category::enum_type || category == category::struct_type);

                if ((category == category::enum_type) && convert_enum_to_underlying)
                {
                    if (is_flags_enum(type))
                    {
                        return fundamental_type::UInt32;
                    }
                    else
                    {
                        return fundamental_type::Int32;
                    }
                }

                return type;
            },
            [](generic_type_instance const& gti) -> type_semantics
            {
                XLANG_ASSERT((gti.generic_type.TypeNamespace() == "Windows.Foundation")
                    && (gti.generic_type.TypeName() == "IReference`1")
                    && gti.generic_args.size() == 1);

                return gti.generic_args[0];
            },
            [](auto v) -> type_semantics { return v; }
        }, get_type_semantics(field.Signature().Type()));
    }

    bool is_customized_struct(TypeDef const& type)
    {
        if (type.TypeNamespace() == "Windows.Foundation")
        {
            static const std::set<std::string_view> custom_structs = { "DateTime", "EventRegistrationToken", "HResult", "TimeSpan" };

            return custom_structs.find(type.TypeName()) != custom_structs.end();
        }

        return false;
    }

    bool has_dealloc(TypeDef const& type)
    {
        auto category = get_category(type);
        return category == category::struct_type ||
            category == category::interface_type ||
            (category == category::class_type && !type.Flags().Abstract());
    }

    bool is_default_constructable(TypeDef const& type)
    {
        if (get_category(type) == category::class_type)
        {
            for (auto&& method : type.MethodList())
            {
                if (is_constructor(method) && empty(method.ParamList()))
                {
                    return true;
                }
            }
        }

        return false;
    }

    bool implements_sequence_protocol(TypeDef const& type)
    {
        return implements_interface(type, "Windows.Foundation.Collections", "IVector`1") 
            || implements_interface(type, "Windows.Foundation.Collections", "IVectorView`1");
    }

    bool implements_mapping_protocol(TypeDef const& type)
    {
        return implements_interface(type, "Windows.Foundation.Collections", "IMap`2")
            || implements_interface(type, "Windows.Foundation.Collections", "IMapView`2");
    }

    bool has_dunder_str_method(TypeDef const& type)
    {
        return implements_interface(type, "Windows.Foundation", "IStringable");
    }

    bool has_dunder_len_method(TypeDef const& type)
    {
        return implements_mapping_protocol(type) || implements_sequence_protocol(type);
    }

    bool has_custom_conversion(TypeDef const& type)
    {
        static const std::set<std::string_view> custom_converters = { "DateTime", "EventRegistrationToken", "HResult", "TimeSpan" };
        if (type.TypeNamespace() == "Windows.Foundation")
        {
            return custom_converters.find(type.TypeName()) != custom_converters.end();
        }

        return false;
    }

    template<typename T>
    bool contains(std::set<T> const& set, T const& value)
    {
        return set.find(value) != set.end();
    }

    template <typename F>
    void enumerate_required_types(TypeDef const& type, F func)
    {
        std::set<TypeDef> types;

        auto enumerate_types_impl = [&](type_semantics const& semantics, auto const& lambda) -> void
        {
            auto type = get_typedef(semantics);
            if (!contains(types, type))
            {
                types.insert(type);
                func(semantics);
            }

            if (get_category(type) == category::interface_type)
            {
                for (auto&& ii : type.InterfaceImpl())
                {
                    lambda(get_type_semantics(ii.Interface()), lambda);
                }
            }
        };

        enumerate_types_impl(type, enumerate_types_impl);
    }

    using method_info = std::tuple<MethodDef, std::vector<type_semantics>>;

    inline bool is_static(std::vector<method_info> const& methods)
    {
        XLANG_ASSERT(methods.size() > 0);
        return is_static(std::get<0>(methods[0]));
    }

    inline bool is_get_method(std::vector<method_info> const& methods)
    {
        XLANG_ASSERT(methods.size() > 0);
        return ((methods.size() == 1) && is_get_method(std::get<0>(methods[0])));
    }

    inline bool is_put_method(std::vector<method_info> const& methods)
    {
        XLANG_ASSERT(methods.size() > 0);
        return ((methods.size() == 1) && is_put_method(std::get<0>(methods[0])));
    }

    auto get_methods(TypeDef const& type)
    {
        std::map<std::string_view, std::vector<method_info>> methods{};

        enumerate_required_types(type, [&](type_semantics const& semantics)
        {
            auto generic_args = std::visit(
                impl::overloaded{
                    [](type_definition) -> std::vector<type_semantics> { return {}; },
                    [](generic_type_instance type_instance) { return type_instance.generic_args; },
                    [](auto) -> std::vector<type_semantics> { throw_invalid("type doesn't contain typedef"); }
                }, semantics);

            for (auto&& method : get_typedef(semantics).MethodList())
            {
                if (is_constructor(method)) continue;

                auto& v = methods[method.Name()];
                XLANG_ASSERT(std::all_of(v.begin(), v.end(), [&method](auto const& t) { return is_static(std::get<0>(t)) == is_static(method); }));
                v.push_back(std::make_tuple(method, std::move(generic_args)));
            }
        });
        return std::move(methods);
    }
}