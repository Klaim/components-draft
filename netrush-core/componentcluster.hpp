#pragma once

#include <vector>
#include <functional>

#include <boost/container/flat_set.hpp>

#include "typeerasureconcurrentmap.hpp"
#include "workqueue.hpp"
#include "componentpool.hpp"
#include "batchcontrollerset.hpp"
#include "trycall.hpp"

namespace netrush {
namespace core {


    /** Manage pools of components and update them in sync.

        Components objects can be of any type that meet the following requirements:
          - must be Movable or Copyable;
          - must be MoveConstructible or CopyConstructible;

        Also:
          - if the component is callable with UpdateArgs arguments, we assume that this is
          the default update function;
          - otherwise if the component is callable with no arguments, we assume that this is
          the default update functoin;
          - otherwise nothing more will be done.
        If a default update function is found, it will be called before the updaters update
        call, but after having applied scheduled task.

        A type of component ComponentType can be used only between `install<ComponentType>()`
        and `uninstall<ComponentType>()` calls.

        Component objects are identified using unique ids provided on construction.
        All the component objects manipulations must provide either a predicate or
        an id to identify the object(s) to operate on.

        @warning Components objects will be moved in memory so the user code should never
        rely on the object's address.


        @remarks All the functions are thread-safe except the `update()` function which
        *must* always be called from one thread at a time.
        All component objects are only modified on the update() call,
        other calls modifying these objects will be delayed until update() is called.

        @remarks This cluster also provide a feature called "operation forwarding":
        if we have a type of component A and a type B inheriting from A;
        and we state that A should forward operations to B, then:
         1. operations on components objects of type A
            with specific id or matching a predicate will be
            forwarded to the components objects of type B which match the same id or
            predicate;
         2. updaters which will process all components of type A will also process
            all components of type B;
        This will be used to allow a component of type B to receive the same commands
        than a component of type A which have the same id.
        This feature is used for implementing "following it like a shadow" kind of virus.


        @tparam UpdateArgs  Arguments that will be passed to the update() function.
    */
    template< class... UpdateArgs >
    class ComponentCluster
    {
    public:

        template< class T >
        using Updater = IndexedBatchController< UUID, T, UpdateArgs... >;

        /** Create a pool for the provided component type.
            This must be called before any other operation involving the component type.

            @tparam ComponentType   Type of component that must be Movable or Copyable and MoveConstructible or CopyConstructible.
            @param  reserved_count  Minimum capacity (number of objects that can be contained) of the pool.
        */
        template< class ComponentType >
        void install( size_t reserved_count = 0 );

        /** Create a pool for the provided component type and makes it receive operations from a parent type.
            The component type to install must inherit from the parent type.
            The parent type must already be registered (TODO: otherwise what?)

            @tparam ParentType      Type of component that must have been installed before.
            @tparam ComponentType   Type of component that must be inherit from ParentType, be Movable or Copyable and MoveConstructible or CopyConstructible.
        */
        template< class ParentType, class ComponentType >
        void install_forwarding( size_t reserved_count = 0 );

        /** Destroy the pool for the provided component type if exists and unregister all associated updaters.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            @remark Once this is function is called, all operations on the component type will do nothing,
                    except calling install() again.

            @remark This is a thread-safe call but will be applied only on the next update() call.


            @tparam ComponentType   Type of component that must have been installed before.
        */
        template< class ComponentType >
        void uninstall();

        /** Make sure that the memory capacity of the pool of the specified component type can contain at least the specified count of components.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )
            If the pool have a lesser capacity than requested, it will grow to a equal or higher capacity than requested;
            otherwise it will do nothing.

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param  count           Minimum capacity (number of objects that can be contained) requested to the pool.
        */
        template< class ComponentType >
        void reserve( size_t count );

        /** Make sure that the memory capacity of the pool of the specified component type can contain at least the specified count of components plus the current component count.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )
            If the pool have a lesser capacity than the requested count plus it's current component count,
            it will grow to a equal or higher capacity than requested added to the current component count;
            otherwise it will do nothing.

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param  count           Minimum capacity (number of objects that can be contained) requested to the pool.
        */
        template< class ComponentType >
        void reserve_extra( size_t count );


        /** Destroys all the component instances of the specified type.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            This does not destroy the pool so new components can be created after this call
            without having to call install().

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
        */
        template< class ComponentType >
        void clear();

        /** Create one instance of the specified component type, using the provided initialization arguments, if any.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param id               Unique identifier of the component object. If the identifier already exists, the operation will do nothing.
            @param init_args        Initialization arguments that will be passed to the component object's constructor.
                                    Can be omitted, which will call the default constructor.
        */
        template< class ComponentType, class... InitArgs >
        void create( UUID id, InitArgs&&... init_args );

        // TODO: add a create_batch( count, IdRange/Generator/Source, init_args ) which create count objects using the same init_args and the provided id source/generator/range - for optimization only!

        /** Destroy one instance of the specified component type which is associated to the provided id, if found.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )
            If the id is not found, do nothing.

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param id               Unique identifier of the component object.
        */
        template< class ComponentType >
        void destroy( UUID id );


        /** Destroy all instances of the specified component type which matches the provided predicate.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param predicate        Predicate taking arguments ( const UUID&, const ComponentType& ) and returning true if the object must be destroyed, false otherwise.
        */
        template< class ComponentType, class Predicate >
        void destroy_if( Predicate&& predicate );

        /** Apply the provided function to each object of the specified component type.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param func             Callable taking arguments ( const UUID&, ComponentType& ) that will be applied to all components.
        */
        template< class ComponentType, class Func >
        void for_each( Func&& func );

        /** Schedule the provided work to be applied to the component of the specified type associated with the provided id.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param id               Id of the component to work on.
            @param work             Callable taking arguments ( ComponentType& ) that will work with the component object.
        */
        template< class ComponentType, class Func >
        void schedule( UUID id, Func&& work );

        /** Schedule work to be applied for each component of the specified type matching the provided predicate.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param predicate        Predicate taking arguments ( const UUID&, const ComponentType& ) and returning true if the component must be worked on.
            @param work             Callable taking arguments ( ComponentType& ) that will work with the component matching the predicate.
        */
        template< class ComponentType, class Predicate, class Func >
        void schedule_if( Predicate&& predicate, Func&& work );

        /** Register an updater for the specified component type which will be called to update objects of that type.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param  updater         Updater object which will be called for updating each component of the same type.
        */
        template< class ComponentType >
        void add_updater( Updater<ComponentType>& updater );

        /** Unregister an updater for the specified component type which will not be called anymore to update objects of that type.
            If the pool for the specified component type is not installed, do nothing. ( @see install() )

            @remark This is a thread-safe call but will be applied only on the next update() call.

            @tparam ComponentType   Type of component that must have been installed before.
            @param  updater         Updater object to unregister.
        */
        template< class ComponentType >
        void remove_updater( Updater<ComponentType>& updater );

        /** Update all contained component objects using the updaters associated with their type.
            @warning This is NOT a thread-safe call and should be only called by one thread at a time.

            @remark Note that for each component in contained in this :
              - if the component is callable with UpdateArgs arguments, we assume that this is
              the default update function;
              - otherwise if the component is callable with no arguments, we assume that this is
              the default update functoin;
              - otherwise nothing more will be done.
            If a default update function is found, it will be called before the updaters update
            call, but after having applied scheduled task.

            @param  args            Arguments to pass to the updaters for updating all their associated components.
        */
        template< class... Args >
        void update( Args&&... args );

        /// @see update()
        template< class... Args >
        inline void operator()( Args&&... args )
        {
            return update( std::forward<Args>(args)... );
        }

    private:

        template< class T > using UpdaterSet = IndexedBatchUpdater< UUID, T, UpdateArgs... >;

        class Slot
        {
            const type_index m_type_idx;

        protected:

            explicit Slot( type_index type_idx ) : m_type_idx( type_idx ) {}

        public:

            virtual ~Slot() = default;

            type_index type_idx() const { return m_type_idx; }

            virtual void update_pool() = 0;
            virtual void update( UpdateArgs... args ) = 0;

        };


        template< class SourceType >
        class Forwarding
        {
        protected:
            Forwarding() = default;
        public:

            // TODO: fix performance issue using these std::function as argument (if possible)
            using WorkOnAny     = std::function< void ( const UUID&, SourceType& ) >;
            using WorkOnTarget  = std::function< void ( SourceType& ) >;
            using Predicate     = std::function< bool ( const UUID&, const SourceType& ) >;


            virtual ~Forwarding() = default;
            virtual void update( UpdaterSet<SourceType>& updaters, UpdateArgs... args ) = 0;
            virtual void for_each( WorkOnAny work ) = 0;
            virtual void schedule( UUID id, WorkOnTarget work ) = 0;
            virtual void schedule_if( Predicate predicate, WorkOnTarget work ) = 0;

        };

        template< class T > class ForwardingList;

        template< class T >
        class SlotFor : public Slot
        {

            ComponentPool<T> m_pool;
            UpdaterSet<T> m_updaters;
            ForwardingList<T> m_forwarding_list;


            template< class... Args >
            void update_if_callable( Args&& ... args )
            {
                try_call_each_indexed( m_pool, std::forward<Args>(args)... );
            }

        public:

            explicit SlotFor( size_t reserved_count )
                : m_pool( reserved_count )
                , Slot( type_id<T>() )
            {}

            void reserve( size_t count )
            {
                m_pool.reserve( count );
            }

            void reserve_extra( size_t count )
            {
                m_pool.reserve_extra( count );
            }

            auto begin()        ->  decltype(m_pool.begin()) { return m_pool.begin(); }
            auto begin() const  ->  decltype(m_pool.begin()) { return m_pool.begin(); }

            auto end()          ->  decltype( m_pool.end() ) { return m_pool.end(); }
            auto end() const    ->  decltype( m_pool.end() ) { return m_pool.end(); }

            template< class K >
            void make_forwarding( SlotFor<K>& target_slot )
            {
                m_forwarding_list.add( target_slot );
            }

            template< class K >
            void remove_forwarding()
            {
                m_forwarding_list.template remove<K>();
            }

            void add_updater( Updater<T>& updater )
            {
                m_updaters.add( updater );
            }

            void remove_updater( Updater<T>& updater )
            {
                m_updaters.remove( updater );
            }

            void update_pool() override
            {
                m_forwarding_list.update_forwardings();
                m_pool.update();
            }

            void update( UpdateArgs... args ) override
            {
                update_if_callable( args... );
                m_updaters.update_indexed_batch( m_pool.begin(), m_pool.end(), args... );
                m_forwarding_list.update( m_updaters, args... );
            }

            void clear()
            {
                m_pool.clear();
            }

            template< class... InitArgs >
            void create( const UUID& id, InitArgs&&... init_args )
            {
                m_pool.create( id, std::forward<InitArgs>(init_args)... );
            }

            void destroy( const UUID& id )
            {
                m_pool.destroy( id );
                // TODO: consider destroying the forwarded objects too! or use a special message?
            }

            template< class Predicate >
            void destroy_if( Predicate&& predicate )
            {
                m_pool.destroy_if( std::forward<Predicate>(predicate) );
                // TODO: consider destroying the forwarded objects too! or use a special message?
            }

            template< class Operation >
            void for_each( Operation&& op )
            {
                m_pool.for_each( std::forward<Operation>(op) );

                m_forwarding_list.for_each( [=]( Forwarding<T>& forwarding ) {
                    forwarding.for_each( op );
                });
            }

            template< class Operation >
            void schedule( const UUID& id, Operation&& op )
            {
                m_pool.schedule( id, std::forward<Operation>( op ) );

                m_forwarding_list.for_each( [=]( Forwarding<T>& forwarding ) {
                    forwarding.schedule( id, op );
                } );
            }

            template< class Predicate, class Operation >
            void schedule_if( Predicate&& predicate, Operation&& op )
            {
                m_pool.schedule_if( std::forward<Predicate>(predicate), std::forward<Operation>(op) );

                m_forwarding_list.for_each( [=]( Forwarding<T>& forwarding ) {
                    forwarding.schedule_if( predicate, op );
                });
            }
        };




        template< class SourceType, class TargetType >
        class Forward : public Forwarding< SourceType >
        {
            static_assert( std::is_base_of< SourceType, TargetType >::value, "SourceType must be a base type of TargetType" );
            SlotFor<TargetType>& m_target_slot;
        public:

            using WorkOnAny = typename Forwarding< SourceType >::WorkOnAny;
            using Predicate = typename Forwarding< SourceType >::Predicate;
            using WorkOnTarget = typename Forwarding< SourceType >::WorkOnTarget;

            explicit Forward( SlotFor<TargetType>& target_slot )
                : m_target_slot( target_slot )
            {}

            void update( UpdaterSet<SourceType>& updaters, UpdateArgs... args ) override
            {
                updaters.update_indexed_batch( m_target_slot.begin(), m_target_slot.end(), args... );
            }

            void for_each( WorkOnAny work ) override
            {
                m_target_slot.for_each( work );
            }

            void schedule( UUID id, WorkOnTarget work ) override
            {
                m_target_slot.schedule( id, work );
            }

            void schedule_if( Predicate predicate, WorkOnTarget work ) override
            {
                m_target_slot.schedule_if( predicate, work );
            }


        };

        template< class T >
        class ForwardingList
        {
        public:

            template< class TargetType >
            void add( SlotFor<TargetType>& target_slot )
            {
                static_assert( std::is_base_of< T, TargetType >::value, "T must be a base type of TargetType" );

                m_work_queue.push( [&] {
                    m_forwarding_index.emplace( type_id<TargetType>(), std::make_unique<Forward<T, TargetType>>( target_slot ) );
                } );
            }

            template< class TargetType >
            void remove()
            {
                m_work_queue.push( [&]{
                    m_forwarding_index.erase( type_id<TargetType>() );
                });
            }

            template< class Operation >
            void for_each( Operation op )
            {
                m_work_queue.push( [=] {
                    for( auto&& forwarding_slot : m_forwarding_index )
                    {
                        auto& forwarding_ptr = forwarding_slot.second;
                        op( *forwarding_ptr );
                    }
                });
            }

            template< class... Args >
            void update( UpdaterSet<T>& updaters, Args&&... args )
            {
                for( auto&& forwarding_slot : m_forwarding_index )
                {
                    auto& forwarding_ptr = forwarding_slot.second;
                    forwarding_ptr->update( updaters, std::forward<Args>(args)... );
                }
            }

            void update_forwardings() const
            {
                m_work_queue.execute();
            }

        private:
            boost::container::flat_map< type_index, std::unique_ptr<Forwarding<T>> > m_forwarding_index;
            mutable core::WorkQueue<> m_work_queue;
        };

        using SlotIndex = TypeErasureConcurrentMap< Slot, SlotFor >;

        template< class T >
        using SlotForPtr = std::shared_ptr<SlotFor<T>>;
        using SlotPtr = std::shared_ptr<Slot>;




        template< class ComponentType >
        SlotForPtr<ComponentType> find_or_create_pool( size_t reserved_count )
        {
            auto slot_ptr = m_slot_index.template find_or_create<ComponentType>( reserved_count );
            UCX_ASSERT_NOT_NULL( slot_ptr );
            slot_ptr->reserve( reserved_count ); // in case it already exists
            m_workqueue.push( [=] {
                m_update_list.emplace_back( slot_ptr );
            } );
            return slot_ptr;
        }




        SlotIndex m_slot_index;
        std::vector<SlotPtr> m_update_list;
        WorkQueue<> m_workqueue;

    };


    template< class... UpdateArgs >
    template< class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        install( size_t reserved_count )
    {
        find_or_create_pool<ComponentType>( reserved_count );
    }

    template< class... UpdateArgs >
    template< class ParentType, class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        install_forwarding( size_t reserved_count )
    {
        if( auto parent_slot_ptr = m_slot_index.template find<ParentType>() )
        {
            auto new_slot_ptr = find_or_create_pool<ComponentType>( reserved_count );
            parent_slot_ptr->make_forwarding( *new_slot_ptr );
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        uninstall()
    {
        m_slot_index.template erase<ComponentType>();

        const type_index type_idx{ type_id<ComponentType>() };
        m_workqueue.push( [=]{
            m_update_list.erase( std::remove_if( begin(m_update_list), end(m_update_list), [=]( const SlotPtr& slot ) {
                return slot->type_idx() == type_idx;
            }), end(m_update_list) );
        } );

    }



    template< class... UpdateArgs >
    template< class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        reserve( size_t count )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->reserve( count );
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        reserve_extra( size_t count )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->reserve_extra( count );
        }
    }


    template< class... UpdateArgs >
    template< class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        clear()
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->clear();
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType, class... InitArgs >
    void ComponentCluster<UpdateArgs...>::
        create( UUID id, InitArgs&&... init_args )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->create( id, std::forward<InitArgs>(init_args)... );
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        destroy( UUID id )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->destroy( id );
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType, class Predicate >
    void ComponentCluster<UpdateArgs...>::
        destroy_if( Predicate&& predicate )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->destroy_if( std::forward<Predicate>(predicate) );
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType, class Func >
    void ComponentCluster<UpdateArgs...>::
        for_each( Func&& func )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->for_each( std::forward<Func>(func) );
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType, class Func >
    void ComponentCluster<UpdateArgs...>::
        schedule( UUID id, Func&& func )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->schedule( id, std::forward<Func>( func ) );
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType, class Predicate, class Func >
    void ComponentCluster<UpdateArgs...>::
        schedule_if( Predicate&& predicate, Func&& func )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            slot->schedule_if( predicate, func );
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        add_updater( Updater<ComponentType>& updater )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            m_workqueue.push( [&,slot]{
                slot->add_updater( updater );
            });
        }
    }

    template< class... UpdateArgs >
    template< class ComponentType >
    void ComponentCluster<UpdateArgs...>::
        remove_updater( Updater<ComponentType>& updater )
    {
        if( auto slot = m_slot_index.template find<ComponentType>() )
        {
            m_workqueue.push( [&, slot] {
                slot->remove_updater( updater );
            } );
        }
    }

    template< class... UpdateArgs >
    template< class... Args >
    void ComponentCluster<UpdateArgs...>::
        update( Args&&... args )
    {
        // NOTE: see http://stackoverflow.com/questions/15844412/in-generic-object-update-loop-is-it-better-to-update-per-controller-or-per-obje/15844441

        m_workqueue.execute();

        for( auto&& slot : m_update_list )
            slot->update_pool();

        for( auto&& slot : m_update_list )
            slot->update( std::forward<Args>(args)... );
    }




}}

