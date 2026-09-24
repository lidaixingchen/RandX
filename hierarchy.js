var hierarchy =
[
    [ "std::bool_constant", null, [
      [ "RandX::detail::has_result_type< E, std::void_t< typename E::result_type > >", "structRandX_1_1detail_1_1has__result__type_3_01E_00_01std_1_1void__t_3_01typename_01E_1_1result__type_01_4_01_4.html", null ],
      [ "RandX::detail::is_character< T >", "structRandX_1_1detail_1_1is__character.html", null ],
      [ "RandX::detail::is_full_32bit_engine< Engine, std::enable_if_t< is_random_engine_v< Engine > > >", "structRandX_1_1detail_1_1is__full__32bit__engine_3_01Engine_00_01std_1_1enable__if__t_3_01is__ra3e0fe64cdb385df8e35af6ffb1fb4704.html", null ],
      [ "RandX::detail::is_rand_fillable< It, T, std::void_t< decltype(*std::declval< It & >()=std::declval< T >()) > >", "structRandX_1_1detail_1_1is__rand__fillable_3_01It_00_01T_00_01std_1_1void__t_3_01decltype_07_5s0388c6a0c5970e1d1a8b3fc926f79b5e.html", null ],
      [ "RandX::detail::is_serializable_engine< E, std::void_t< decltype(std::declval< const E & >().serialize()), decltype(std::declval< E & >().deserialize(std::declval< typename E::state_type >())), typename E::state_type > >", "structRandX_1_1detail_1_1is__serializable__engine_3_01E_00_01std_1_1void__t_3_01decltype_07std_16005f2a699fa7de44dcacc230bed4880.html", null ]
    ] ],
    [ "std::bool_constant&lt; std::is_same_v&lt; decltype(E::min()), E::result_type &gt; &amp;&amp;std::is_same_v&lt; decltype(E::max()), E::result_type &gt; &amp;&amp;(E::min()&lt; E::max())&gt;", null, [
      [ "RandX::detail::has_min_max< E, std::enable_if_t< has_invocable_engine< E >::value, std::void_t< decltype(E::min()), decltype(E::max()), std::integral_constant< bool,(E::min()< E::max())> > > >", "structRandX_1_1detail_1_1has__min__max_3_01E_00_01std_1_1enable__if__t_3_01has__invocable__engine5b1a94a1a5a1f748eb82bd5282be940.html", null ]
    ] ],
    [ "std::bool_constant&lt;(sizeof(std::remove_cv_t&lt; std::remove_reference_t&lt; Engine &gt; &gt;::result_type) &gt;=sizeof(std::uint64_t) &amp;&amp;static_cast&lt; std::uint64_t &gt;(std::remove_cv_t&lt; std::remove_reference_t&lt; Engine &gt; &gt;::min())==0ULL &amp;&amp;static_cast&lt; std::uint64_t &gt;(std::remove_cv_t&lt; std::remove_reference_t&lt; Engine &gt; &gt;::max())==(std::numeric_limits&lt; std::uint64_t &gt;::max)())&gt;", null, [
      [ "RandX::detail::is_full_64bit_engine< Engine, std::enable_if_t< is_random_engine_v< Engine > > >", "structRandX_1_1detail_1_1is__full__64bit__engine_3_01Engine_00_01std_1_1enable__if__t_3_01is__rab2a5847a3709cf1e7035ca2508caa90b.html", null ]
    ] ],
    [ "RandX::ChaCha20", "classRandX_1_1ChaCha20.html", null ],
    [ "RandX::detail::DecomposedGammaLog&lt; WorkT &gt;", "structRandX_1_1detail_1_1DecomposedGammaLog.html", null ],
    [ "RandX::detail::EngineBase&lt; Derived, ResultType, N &gt;", "structRandX_1_1detail_1_1EngineBase.html", null ],
    [ "RandX::detail::EngineBase&lt; RomuDuoJr, std::uint64_t, 2 &gt;", "structRandX_1_1detail_1_1EngineBase.html", [
      [ "RandX::RomuDuoJr", "classRandX_1_1RomuDuoJr.html", null ],
      [ "RandX::RomuDuoJr", "classRandX_1_1RomuDuoJr.html", null ]
    ] ],
    [ "RandX::detail::EngineBase&lt; SFC64, std::uint64_t, 4 &gt;", "structRandX_1_1detail_1_1EngineBase.html", [
      [ "RandX::SFC64", "classRandX_1_1SFC64.html", null ],
      [ "RandX::SFC64", "classRandX_1_1SFC64.html", null ]
    ] ],
    [ "RandX::detail::EngineBase&lt; Xoroshiro128StarStar, std::uint64_t, 2 &gt;", "structRandX_1_1detail_1_1EngineBase.html", [
      [ "RandX::Xoroshiro128StarStar", "classRandX_1_1Xoroshiro128StarStar.html", null ],
      [ "RandX::Xoroshiro128StarStar", "classRandX_1_1Xoroshiro128StarStar.html", null ]
    ] ],
    [ "RandX::detail::EngineBase&lt; Xoroshiro64StarStar, std::uint32_t, 2 &gt;", "structRandX_1_1detail_1_1EngineBase.html", [
      [ "RandX::Xoroshiro64StarStar", "classRandX_1_1Xoroshiro64StarStar.html", null ],
      [ "RandX::Xoroshiro64StarStar", "classRandX_1_1Xoroshiro64StarStar.html", null ]
    ] ],
    [ "RandX::detail::EngineBase&lt; Xoshiro128StarStar, std::uint32_t, 4 &gt;", "structRandX_1_1detail_1_1EngineBase.html", [
      [ "RandX::Xoshiro128StarStar", "classRandX_1_1Xoshiro128StarStar.html", null ],
      [ "RandX::Xoshiro128StarStar", "classRandX_1_1Xoshiro128StarStar.html", null ]
    ] ],
    [ "RandX::detail::EngineBase&lt; Xoshiro256StarStar, std::uint64_t, 4 &gt;", "structRandX_1_1detail_1_1EngineBase.html", [
      [ "RandX::Xoshiro256StarStar", "classRandX_1_1Xoshiro256StarStar.html", null ],
      [ "RandX::Xoshiro256StarStar", "classRandX_1_1Xoshiro256StarStar.html", null ]
    ] ],
    [ "RandX::detail::EngineStatePolicy&lt; Engine &gt;", "structRandX_1_1detail_1_1EngineStatePolicy.html", null ],
    [ "RandX::detail::EngineStatePolicy&lt; RandX::SFC64 &gt;", "structRandX_1_1detail_1_1EngineStatePolicy_3_01RandX_1_1SFC64_01_4.html", null ],
    [ "std::false_type", null, [
      [ "RandX::detail::HasJump< Engine, std::void_t< decltype(std::declval< Engine & >().jump())> >", "structRandX_1_1detail_1_1HasJump_3_01Engine_00_01std_1_1void__t_3_01decltype_07std_1_1declval_3_54a1b52567defa45999d2be9a476b86a.html", null ],
      [ "RandX::detail::HasLongJump< Engine, std::void_t< decltype(std::declval< Engine & >().longJump())> >", "structRandX_1_1detail_1_1HasLongJump_3_01Engine_00_01std_1_1void__t_3_01decltype_07std_1_1declva399cfb803081c83cb97537fa57c762c1.html", null ],
      [ "RandX::detail::has_invocable_engine< E, std::enable_if_t< has_result_type< E >::value, std::void_t< decltype(std::declval< E & >()())> > >", "structRandX_1_1detail_1_1has__invocable__engine_3_01E_00_01std_1_1enable__if__t_3_01has__result_03de6a5b82a6d887f79fd988198ec06d.html", null ],
      [ "RandX::detail::has_min_max< std::remove_cv_t< std::remove_reference_t< E > > >", "structRandX_1_1detail_1_1has__min__max.html", [
        [ "RandX::detail::is_random_engine< E >", "structRandX_1_1detail_1_1is__random__engine.html", null ]
      ] ],
      [ "RandX::detail::has_min_max< E, std::enable_if_t< has_invocable_engine< E >::value, std::void_t< decltype(E::min()), decltype(E::max()), std::integral_constant< bool,(E::min()< E::max())> > > >", "structRandX_1_1detail_1_1has__min__max_3_01E_00_01std_1_1enable__if__t_3_01has__invocable__engine5b1a94a1a5a1f748eb82bd5282be940.html", null ],
      [ "RandX::detail::has_result_type< E, std::void_t< typename E::result_type > >", "structRandX_1_1detail_1_1has__result__type_3_01E_00_01std_1_1void__t_3_01typename_01E_1_1result__type_01_4_01_4.html", null ],
      [ "RandX::detail::is_full_32bit_engine< Engine, std::enable_if_t< is_random_engine_v< Engine > > >", "structRandX_1_1detail_1_1is__full__32bit__engine_3_01Engine_00_01std_1_1enable__if__t_3_01is__ra3e0fe64cdb385df8e35af6ffb1fb4704.html", null ],
      [ "RandX::detail::is_full_64bit_engine< Engine, std::enable_if_t< is_random_engine_v< Engine > > >", "structRandX_1_1detail_1_1is__full__64bit__engine_3_01Engine_00_01std_1_1enable__if__t_3_01is__rab2a5847a3709cf1e7035ca2508caa90b.html", null ],
      [ "RandX::detail::is_indexable_state< S, std::void_t< decltype(std::declval< const S & >().size()), decltype(std::declval< S & >()[std::size_t{}]), typename S::value_type > >", "structRandX_1_1detail_1_1is__indexable__state_3_01S_00_01std_1_1void__t_3_01decltype_07std_1_1dee83c4b6b998d26456da40247da3b1392.html", null ],
      [ "RandX::detail::is_input_iterator< It, std::void_t< typename std::iterator_traits< It >::iterator_category > >", "structRandX_1_1detail_1_1is__input__iterator_3_01It_00_01std_1_1void__t_3_01typename_01std_1_1itc9507629b63827a72627b5cfc13a2762.html", null ],
      [ "RandX::detail::is_rand_fillable< It, T, std::void_t< decltype(*std::declval< It & >()=std::declval< T >()) > >", "structRandX_1_1detail_1_1is__rand__fillable_3_01It_00_01T_00_01std_1_1void__t_3_01decltype_07_5s0388c6a0c5970e1d1a8b3fc926f79b5e.html", null ],
      [ "RandX::detail::is_random_access_container< C, std::void_t< decltype(std::begin(std::declval< C & >())), decltype(std::end(std::declval< C & >())), decltype(std::size(std::declval< C & >()))> >", "structRandX_1_1detail_1_1is__random__access__container_3_01C_00_01std_1_1void__t_3_01decltype_0730445ba85b1cba4caf94f7004a184a57.html", null ],
      [ "RandX::detail::is_random_access_iterator< decltype(std::begin(std::declval< C & >()))>", "structRandX_1_1detail_1_1is__random__access__iterator.html", [
        [ "RandX::detail::is_random_access_container< C, std::void_t< decltype(std::begin(std::declval< C & >())), decltype(std::end(std::declval< C & >())), decltype(std::size(std::declval< C & >()))> >", "structRandX_1_1detail_1_1is__random__access__container_3_01C_00_01std_1_1void__t_3_01decltype_0730445ba85b1cba4caf94f7004a184a57.html", null ]
      ] ],
      [ "RandX::detail::is_random_access_iterator< It, std::void_t< typename std::iterator_traits< It >::iterator_category > >", "structRandX_1_1detail_1_1is__random__access__iterator_3_01It_00_01std_1_1void__t_3_01typename_01906678429c844b6cd0d6d7a800b7ca40.html", null ],
      [ "RandX::detail::is_seed_sequence_helper< S, std::void_t< decltype(std::declval< S & >().generate(std::declval< std::uint32_t * >(), std::declval< std::uint32_t * >())) > >", "structRandX_1_1detail_1_1is__seed__sequence__helper_3_01S_00_01std_1_1void__t_3_01decltype_07stdebc0e815b75e5f80c042552eb35c88f6.html", null ],
      [ "RandX::detail::is_serializable_engine< E, std::void_t< decltype(std::declval< const E & >().serialize()), decltype(std::declval< E & >().deserialize(std::declval< typename E::state_type >())), typename E::state_type > >", "structRandX_1_1detail_1_1is__serializable__engine_3_01E_00_01std_1_1void__t_3_01decltype_07std_16005f2a699fa7de44dcacc230bed4880.html", null ],
      [ "RandX::detail::HasJump< Engine, class >", "structRandX_1_1detail_1_1HasJump.html", null ],
      [ "RandX::detail::HasLongJump< Engine, class >", "structRandX_1_1detail_1_1HasLongJump.html", null ],
      [ "RandX::detail::has_invocable_engine< E, class >", "structRandX_1_1detail_1_1has__invocable__engine.html", null ],
      [ "RandX::detail::has_min_max< E, class >", "structRandX_1_1detail_1_1has__min__max.html", null ],
      [ "RandX::detail::has_result_type< E, class >", "structRandX_1_1detail_1_1has__result__type.html", null ],
      [ "RandX::detail::is_full_32bit_engine< Engine, class >", "structRandX_1_1detail_1_1is__full__32bit__engine.html", null ],
      [ "RandX::detail::is_full_64bit_engine< Engine, class >", "structRandX_1_1detail_1_1is__full__64bit__engine.html", null ],
      [ "RandX::detail::is_indexable_state< S, class >", "structRandX_1_1detail_1_1is__indexable__state.html", null ],
      [ "RandX::detail::is_input_iterator< It, class >", "structRandX_1_1detail_1_1is__input__iterator.html", null ],
      [ "RandX::detail::is_rand_fillable< It, T, class >", "structRandX_1_1detail_1_1is__rand__fillable.html", null ],
      [ "RandX::detail::is_random_access_container< C, class >", "structRandX_1_1detail_1_1is__random__access__container.html", null ],
      [ "RandX::detail::is_random_access_iterator< It, class >", "structRandX_1_1detail_1_1is__random__access__iterator.html", null ],
      [ "RandX::detail::is_seed_sequence_helper< S, class >", "structRandX_1_1detail_1_1is__seed__sequence__helper.html", null ],
      [ "RandX::detail::is_serializable_engine< E, class >", "structRandX_1_1detail_1_1is__serializable__engine.html", null ]
    ] ],
    [ "std::integral_constant", null, [
      [ "RandX::detail::is_seed_sequence< S >", "structRandX_1_1detail_1_1is__seed__sequence.html", null ]
    ] ],
    [ "std::is_base_of", null, [
      [ "RandX::detail::is_input_iterator< It, std::void_t< typename std::iterator_traits< It >::iterator_category > >", "structRandX_1_1detail_1_1is__input__iterator_3_01It_00_01std_1_1void__t_3_01typename_01std_1_1itc9507629b63827a72627b5cfc13a2762.html", null ],
      [ "RandX::detail::is_random_access_iterator< It, std::void_t< typename std::iterator_traits< It >::iterator_category > >", "structRandX_1_1detail_1_1is__random__access__iterator_3_01It_00_01std_1_1void__t_3_01typename_01906678429c844b6cd0d6d7a800b7ca40.html", null ]
    ] ],
    [ "std::is_same", null, [
      [ "RandX::detail::has_invocable_engine< E, std::enable_if_t< has_result_type< E >::value, std::void_t< decltype(std::declval< E & >()())> > >", "structRandX_1_1detail_1_1has__invocable__engine_3_01E_00_01std_1_1enable__if__t_3_01has__result_03de6a5b82a6d887f79fd988198ec06d.html", null ],
      [ "RandX::detail::is_indexable_state< S, std::void_t< decltype(std::declval< const S & >().size()), decltype(std::declval< S & >()[std::size_t{}]), typename S::value_type > >", "structRandX_1_1detail_1_1is__indexable__state_3_01S_00_01std_1_1void__t_3_01decltype_07std_1_1dee83c4b6b998d26456da40247da3b1392.html", null ]
    ] ],
    [ "RandX::detail::PreparedWeights&lt; WeightContainer &gt;", "structRandX_1_1detail_1_1PreparedWeights.html", null ],
    [ "RandX::detail::RealIntervalKernel&lt; T &gt;", "classRandX_1_1detail_1_1RealIntervalKernel.html", null ],
    [ "RandX::detail::ScopedWiper", "structRandX_1_1detail_1_1ScopedWiper.html", null ],
    [ "RandX::SplitMix64", "classRandX_1_1SplitMix64.html", null ],
    [ "RandX::detail::StreamFormatGuard&lt; CharT, Traits &gt;", "classRandX_1_1detail_1_1StreamFormatGuard.html", null ],
    [ "std::true_type", null, [
      [ "RandX::detail::HasJump< Engine, std::void_t< decltype(std::declval< Engine & >().jump())> >", "structRandX_1_1detail_1_1HasJump_3_01Engine_00_01std_1_1void__t_3_01decltype_07std_1_1declval_3_54a1b52567defa45999d2be9a476b86a.html", null ],
      [ "RandX::detail::HasLongJump< Engine, std::void_t< decltype(std::declval< Engine & >().longJump())> >", "structRandX_1_1detail_1_1HasLongJump_3_01Engine_00_01std_1_1void__t_3_01decltype_07std_1_1declva399cfb803081c83cb97537fa57c762c1.html", null ],
      [ "RandX::detail::is_seed_sequence_helper< S, std::void_t< decltype(std::declval< S & >().generate(std::declval< std::uint32_t * >(), std::declval< std::uint32_t * >())) > >", "structRandX_1_1detail_1_1is__seed__sequence__helper_3_01S_00_01std_1_1void__t_3_01decltype_07stdebc0e815b75e5f80c042552eb35c88f6.html", null ]
    ] ]
];