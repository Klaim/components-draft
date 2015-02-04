#pragma once

#include <vector>
#include <memory>
#include <tbb/concurrent_unordered_map.h>

#include "typeindex.hpp"
#include <boost/functional/hash.hpp>

#include "monitoredobjectpool.hpp"


namespace netrush {
namespace core {

    /** A system of objects which are all updated in sync.
        This object will contain pools of objects declared using create_pool().
        These pools of objects will be able to create and destroy instances in an
        asynchronous way.
        All these objects will be updated in sync at each update() call.
        @par
        The goal of this system is to keep all the objects updated in sync
        while allowing users to add work to do for the next update.
        It's meant to be used for example when you have a thread updating
        objects and several other threads manipulating these objects.
        The Monitor<> class is then used to allow safe async manipulations,
        and the pool manipulations are all executed only on system udpate.

        @tparam UpdateArgs      Types of the data that should be provided to all controllers on update.
    */
    template< class... UpdateArgs >
    class MonitoredCluster
    {
    public:

        template< class ObjectType > using Handle           = MonitoredHandle<ObjectType, UpdateArgs...>;
        template< class ObjectType > using ControllerType   = typename Handle<ObjectType>::ControllerType;
        template< class ObjectType > using Monitor          = typename Handle<ObjectType>::MonitorType;
        template< class ObjectType > using ObjectPool       = typename Handle<ObjectType>::ObjectPool;
        template< class ObjectType > using FutureHandle     = future<Handle<ObjectType>>;
        template< class ObjectType > using PromiseHandle    = promise<Handle<ObjectType>>;

        MonitoredCluster( const MonitoredCluster& )            = delete;
        MonitoredCluster& operator=( const MonitoredCluster& ) = delete;

        MonitoredCluster() = default;

        /** Create a pool of objects.
            The provided size is used to reserve memory,
            but the pool will grow when needed.
            No instances are created yet by calling this function.
            @remark Calling this function is required before using all other functions for ObjectType.

            @param reserved_size        Minimum size reserved in memory from the beginning.
            @return true if the pool don't exists already and have been created, false otherwise.
        */
        template< class ObjectType >
        bool create_pool( size_t reserved_size = 0 );

        /** Destroy all instances of the provided ObjectType and destroy the pool.
            @remark Once called, create() and destroy() can't be called for this object type.
            @warning Calling this while smart pointers to monitors are still alive will
                     make them invalid, triggering undefined behaviour if you use them.
                     Fortunately, once destroy_pool() is called it is ok to let the
                     smart pointer be destroyed or reset them.

            @return true if the pool have been found and destroyed, false otherwise.
        */
        template< class ObjectType >
        bool destroy_pool();

        /** Create and provide one instance of the specified type.
            The object is then accessed through a monitored wrapper provided by a future.
            @warning    The future will be blocking until update() is called:
                        Don't call .get() in the same thread than update()!

            @return A future providing a managed pointer to the monitor to the object or null if the object can't be created.
                    Note that the managed pointer will manage the lifetime of the monitor:
                    If you wish to destroy the object (and it's monitor), just reset it or make it go out of scope.
                    If you with the object to be alive until the object pool is destroyed, just release() the pointer.
                    Note that the real destruction of the object will not occur until the next update() call,
                    or the destruction of the object pool.

        */
        template< class ObjectType >
        auto create() -> FutureHandle<ObjectType>;

        template< class ObjectType, class... InitData >
        auto create( InitData&&... init_data )-> FutureHandle<ObjectType>;

        template< class ObjectType >
        void create_then_set( Handle<ObjectType>& handle );

        template< class ObjectType, class... InitData >
        void create_then_set( Handle<ObjectType>& handle, InitData&&... init_data );

        template< class ObjectType, class WorkOnMonitor >
        void create_then( WorkOnMonitor&& work );

        template< class ObjectType, class WorkOnMonitor, class... InitData >
        void create_then( WorkOnMonitor&& work, InitData&&... init_data );


        /* Same as create but don't return a future: the object will live until the pool is destroyed. */
        template< class ObjectType >
        void create_standalone();

        template< class ObjectType, class... InitData >
        void create_standalone( InitData&&... init_data );

        /** Add a controller for the specified object type.
            The controller will then be used for updating all instances of this type.
            @remark The registration will be effective on the next update() call.
        */
        template< class ObjectType >
        void add_controller( BatchController<ObjectType,UpdateArgs...>& controller );

        /** Remove a controller for the specified object type.
            @remark The unregistration will be effective on the next update() call.
        */
        template< class ObjectType >
        void remove_controller( BatchController<ObjectType,UpdateArgs...>& controller );

        /** Update all instances in sync.
            The update goes through each object pool;
            each object pool will update all their instances using the registered controllers;
            each instance will first apply it's work queue (for one-time work) and then be updated by controllers.

            @param update_data      Data provided to all the controllers updating the objects.
        */
        template< class... Args >
        void update( Args&&... update_args );

        /** @see update()
        */
        template< class... Args >
        void operator()( Args&&... update_args ) { update( std::forward<Args>(update_args)... ); }


    private:

        using ObjectPoolPtr     = std::shared_ptr< MonitoredObjectPoolInterface<UpdateArgs...> >;
        using ObjectPoolIndex   = tbb::concurrent_unordered_map< type_index, ObjectPoolPtr, boost::hash<type_index> >;
        using ObjectPoolList    = std::vector<ObjectPoolPtr>;

        ObjectPoolList m_pool_update_list;
        ObjectPoolIndex m_object_pool_index;
        WorkQueue<void> m_work_queue;

        ObjectPoolPtr find_valid_pool( const type_index& type_idx );

        template< class ObjectType >
        std::shared_ptr<ObjectPool<ObjectType>> find_specific_pool();

        template< class ObjectType, class WorkOnPoolFound, class OnPoolNotFound >
        void work_on_pool_or( WorkOnPoolFound&& work, OnPoolNotFound&& on_pool_not_found );

        template< class ObjectType, class WorkOnPoolFound >
        void work_on_pool_or( WorkOnPoolFound&& work );


        template< class ObjectType, class CreationStatement >
        FutureHandle<ObjectType> create_future_monitor( CreationStatement&& creation_statement );

        template< class ObjectType >
        void destroy( Monitor<ObjectType>& object );

    };

    template< class... UpdateArgs >
    template< class ObjectType >
    bool MonitoredCluster<UpdateArgs...>::create_pool( size_t reserve_size )
    {
        const auto& id_info = type_id<ObjectType>();
        if( find_valid_pool( id_info ) )
        {
            // already exists!
            UCX_ASSERT_IMPOSSIBLE( "Tried to create a pool of " << id_info.name() << " in MonitoredCluster but there is already one!" );
            return false;
        }
        else
        {
            auto object_pool = std::make_shared< ObjectPool<ObjectType> >( reserve_size );
            m_object_pool_index[ id_info ] = object_pool;
            m_work_queue.push( [this, object_pool]{ m_pool_update_list.emplace_back( object_pool ); } );
        }
        return true;
    }

    template< class... UpdateArgs >
    template< class ObjectType >
    bool MonitoredCluster<UpdateArgs...>::destroy_pool()
    {
        const auto& id_info = type_id<ObjectType>();
        auto find_it = m_object_pool_index.find( id_info ); // we need the iterator here.
        if( find_it != end(m_object_pool_index) && find_it->second )
        {
            // we can't remove the slot from the concurrent map safely so we just delete the pool and make the pointer null.
            auto object_pool = find_it->second;
            find_it->second.reset();

            // however we can remove the pool from the vector safely with the work queue
            m_work_queue.push( [this, object_pool]{
                m_pool_update_list.erase(
                    std::remove( begin(m_pool_update_list), end(m_pool_update_list), object_pool )
                    , end(m_pool_update_list)
                );
            });

        }
        else
        {
            // don't exists!
            UCX_ASSERT_IMPOSSIBLE( "Tried to destroy a pool of " << id_info.pretty_name() << " in MonitoredCluster but it don't exists!" );
            return false;
        }

        return true;
    }

    template< class... UpdateArgs >
    template< class ObjectType, class WorkOnPoolFound, class OnPoolNotFound >
    void MonitoredCluster<UpdateArgs...>
        ::work_on_pool_or( WorkOnPoolFound&& work, OnPoolNotFound&& on_pool_not_found )
    {
        auto object_pool = find_specific_pool<ObjectType>();
        UCX_ASSERT( object_pool, "Couldn't find the object pool for type " << type_id<ObjectType>().pretty_name() << " : create_pool() have to be called first!" );
        if( object_pool )
            work( *object_pool );
        else
            on_pool_not_found();
    }

    template< class... UpdateArgs >
    template< class ObjectType, class WorkOnPoolFound >
    void MonitoredCluster<UpdateArgs...>
        ::work_on_pool_or( WorkOnPoolFound&& work )
    {
        work_on_pool_or<ObjectType>( std::forward<WorkOnPoolFound>(work), []{} );
    }

    template< class... UpdateArgs >
    template< class ObjectType, class CreationStatement >
    auto MonitoredCluster<UpdateArgs...>
        ::create_future_monitor( CreationStatement&& creation_statement ) -> FutureHandle<ObjectType>
    {
        FutureHandle<ObjectType>result;

        auto on_found = [&]( ObjectPool<ObjectType>& object_pool ) {
            result = creation_statement( object_pool );
        };

        auto on_not_found = [&]{
            result = make_ready_future( Handle<ObjectType>{} );
        };

        work_on_pool_or<ObjectType>( on_found, on_not_found );

        return result;
    }

    template< class... UpdateArgs >
    template< class ObjectType >
    auto MonitoredCluster<UpdateArgs...>
        ::create() -> FutureHandle<ObjectType>
    {
        return create_future_monitor<ObjectType>( []( ObjectPool<ObjectType>& object_pool ) {
            return object_pool.create();
        } );
    }

    template< class... UpdateArgs >
    template< class ObjectType, class... InitData >
    auto MonitoredCluster<UpdateArgs...>
        ::create( InitData&&... init_data ) -> FutureHandle<ObjectType>
    {
        return create_future_monitor<ObjectType>( [&]( ObjectPool<ObjectType>& object_pool ) {
            return object_pool.create( init_data... );
        } );
    }

    template< class... UpdateArgs >
    template< class ObjectType >
    void MonitoredCluster<UpdateArgs...>
        ::create_then_set( Handle<ObjectType>& handle )
    {
        work_on_pool_or<ObjectType>(
            [&]( ObjectPool<ObjectType>& object_pool ) {
                object_pool.create_then_set( handle );
            }
        );
    }

    template< class... UpdateArgs >
    template< class ObjectType, class... InitData >
    void MonitoredCluster<UpdateArgs...>
        ::create_then_set( Handle<ObjectType>& handle, InitData&&... init_data )
    {
        work_on_pool_or<ObjectType>(
            [&]( ObjectPool<ObjectType>& object_pool ) {
                object_pool.create_then_set( handle, std::forward<InitData>(init_data)... );
            }
        );
    }

    template< class... UpdateArgs >
    template< class ObjectType, class WorkOnMonitor >
    void MonitoredCluster<UpdateArgs...>
        ::create_then( WorkOnMonitor&& work )
    {
        work_on_pool_or<ObjectType>(
            [&]( ObjectPool<ObjectType>& object_pool ) {
                object_pool.create_then( std::forward<WorkOnMonitor>(work) );
            }
        );
    }


    template< class... UpdateArgs >
    template< class ObjectType, class WorkOnMonitor, class... InitData >
    void MonitoredCluster<UpdateArgs...>
        ::create_then( WorkOnMonitor&& work, InitData&&... init_data )
    {
        work_on_pool_or<ObjectType>(
            [&]( ObjectPool<ObjectType>& object_pool ) {
                object_pool.create_then( std::forward<WorkOnMonitor>(work), std::forward<InitData>(init_data)... );
            }
        );
    }

    template< class... UpdateArgs >
    template< class ObjectType >
    void MonitoredCluster<UpdateArgs...>
        ::create_standalone()
    {
        work_on_pool_or<ObjectType>(
            [&]( ObjectPool<ObjectType>& object_pool ) {
                object_pool.create_standalone();
            }
        );
    }

    template< class... UpdateArgs >
    template< class ObjectType, class... InitData >
    void MonitoredCluster<UpdateArgs...>
        ::create_standalone( InitData&&... init_data )
    {
        work_on_pool_or<ObjectType>(
            [&]( ObjectPool<ObjectType>& object_pool ) {
                object_pool.create_standalone( std::forward<InitData>(init_data)... );
            }
        );
    }

    template< class... UpdateArgs >
    template< class ObjectType >
    void MonitoredCluster<UpdateArgs...>::destroy( Monitor<ObjectType>& object )
    {
        auto object_pool = find_specific_pool<ObjectType>();
        UCX_ASSERT( object_pool, "Couldn't find the object pool for type " << type_id<ObjectType>().pretty_name() << " : create_pool() have to be called first!" );

        object_pool->destroy( object );
    }

    template< class... UpdateArgs >
    template< class ObjectType >
    void MonitoredCluster<UpdateArgs...>::add_controller( BatchController<ObjectType,UpdateArgs...>& controller )
    {
        auto object_pool = find_specific_pool<ObjectType>();
        UCX_ASSERT( object_pool, "Couldn't find the object pool for type " << type_id<ObjectType>().pretty_name() << " : create_pool() have to be called first!" );

        object_pool->add_controller( controller );
    }

    template< class... UpdateArgs >
    template< class ObjectType >
    void MonitoredCluster<UpdateArgs...>::remove_controller( BatchController<ObjectType,UpdateArgs...>& controller )
    {
        auto object_pool = find_specific_pool<ObjectType>();
        UCX_ASSERT( object_pool, "Couldn't find the object pool for type " << type_id<ObjectType>().pretty_name() << " : create_pool() have to be called first!" );

        object_pool->remove_controller( controller );
    }


    template< class... UpdateArgs >
    template< class... Args >
    void MonitoredCluster<UpdateArgs...>::update( Args&&... update_args )
    {
        m_work_queue.execute();

        for( auto& object_pool : m_pool_update_list )
            object_pool->update( update_args... );
    }

    template< class... UpdateArgs >
    auto MonitoredCluster<UpdateArgs...>
        ::find_valid_pool( const type_index& type_idx ) -> ObjectPoolPtr
    {
        auto find_it = m_object_pool_index.find( type_idx );
        if( find_it != end(m_object_pool_index) && find_it->second )
        {
            return find_it->second;
        }
        return {};
    }

    template< class... UpdateArgs >
    template< class ObjectType >
    auto MonitoredCluster<UpdateArgs...>
        ::find_specific_pool() -> std::shared_ptr< ObjectPool<ObjectType> >
    {
        return std::static_pointer_cast< ObjectPool<ObjectType> >( find_valid_pool( type_id<ObjectType>() ) );
    }

}}


