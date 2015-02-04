#pragma once

#include <memory>
#include <boost/container/stable_vector.hpp>

#include <utilcxx/assert.hpp>
#include <utilcxx/move_on_copy.hpp>

#include "monitor.hpp"
#include "controller.hpp"
#include "batchcontrollerset.hpp"
#include "async.hpp"

namespace netrush {
namespace core {


    template< class ObjectType, class... UpdateArgs >   class MonitoredObjectPool;

    template< class ObjectType, class... UpdateArgs >
    class MonitoredHandle
    {
    public:
        using Monitor       = Monitor<ObjectType, UpdateArgs...>;
        using Controller    = BatchController<ObjectType, UpdateArgs...>;
        using ObjectPool    = MonitoredObjectPool<ObjectType, UpdateArgs...>;


        MonitoredHandle() : m_monitor(), m_pool() {}
        ~MonitoredHandle() { destruction(); }

        // Movable but not copyable
        MonitoredHandle( const MonitoredHandle& )            = delete;
        MonitoredHandle& operator=( const MonitoredHandle& ) = delete;

        MonitoredHandle( MonitoredHandle&& other )
            : m_monitor( std::move(other.m_monitor) )
            , m_pool( std::move(other.m_pool) )
        {
            UCX_ASSERT_NOT_NULL( m_monitor );
            other.m_monitor = nullptr;
        }

        MonitoredHandle& operator=( MonitoredHandle&& other )
        {
            destruction();

            m_monitor = std::move(other.m_monitor);
            m_pool = std::move(other.m_pool);
            other.m_monitor = nullptr;

            return *this;
        }

        template< class Func >
        void schedule( Func&& f )
        {
            UCX_ASSERT_TRUE( is_valid() );
            m_monitor->schedule( std::forward<Func>(f) );
        }

        ObjectType& unsafe_get() { UCX_ASSERT_TRUE( is_valid() ); return m_monitor->unsafe_get(); }
        const ObjectType& unsafe_get() const { UCX_ASSERT_TRUE( is_valid() ); return m_monitor->unsafe_get(); }

        bool is_valid() const { return m_monitor != nullptr && !m_pool.expired(); }

        explicit operator bool() const { return is_valid(); }

    private:

        friend ObjectPool; // built only by object pools
        MonitoredHandle( Monitor& monitor, const std::shared_ptr<ObjectPool>& pool )
            : m_monitor( &monitor )
            , m_pool( pool )
        {
            UCX_ASSERT_TRUE( is_valid() );
        }

        Monitor* m_monitor;
        std::weak_ptr<ObjectPool> m_pool;

        void destruction()
        {
            if( m_monitor )
            {
                if( auto pool = m_pool.lock() ) // ignore the destruction if the pool is already destroyed
                {
                    pool->destroy( *m_monitor );
                }
            }

        }

    };

    template< class ObjectType, class... UpdateArgs >
    inline bool is_valid( const MonitoredHandle<ObjectType,UpdateArgs...>& handle )
    {
        return handle.is_valid();
    }

    template< class... UpdateArgs >
    class MonitoredObjectPoolInterface
    {
    public:
        virtual void update( UpdateArgs... ) = 0;

    protected:
        ~MonitoredObjectPoolInterface(){}
    };

    template< class ObjectType, class... UpdateArgs >
    class MonitoredObjectPool
        : public MonitoredObjectPoolInterface<UpdateArgs...>
        , public std::enable_shared_from_this<MonitoredObjectPool<ObjectType,UpdateArgs...>>
    {
    public:
        using Handle            = MonitoredHandle<ObjectType, UpdateArgs...>;
        using Controller        = typename Handle::Controller;
        using Monitor           = typename Handle::Monitor;

        MonitoredObjectPool( const MonitoredObjectPool& )            = delete;
        MonitoredObjectPool& operator=( const MonitoredObjectPool& ) = delete;

        explicit MonitoredObjectPool( size_t reserve_size );

        future<Handle> create();

        template< class... InitData >
        future<Handle> create( InitData... init_data );

        void create_then_set( Handle& handle );

        template< class... InitData >
        void create_then_set( Handle& handle, InitData&&... init_data );

        template< class WorkOnMonitor >
        void create_then( WorkOnMonitor&& work );

        template< class WorkOnMonitor, class... InitData >
        void create_then( WorkOnMonitor&& work, InitData... init_data );

        void create_standalone();

        template< class... InitData >
        void create_standalone( InitData... init_data );

        void destroy( Monitor& object );

        void update( UpdateArgs... update_args ) override;

        void add_controller( Controller& controller );
        void remove_controller( Controller& controller );

    private:

        using InstancePool      = boost::container::stable_vector< ObjectType >;
        using MonitorPool       = boost::container::stable_vector< Monitor >;
        using ControllerList    = BatchUpdater< ObjectType, UpdateArgs... >;

        InstancePool m_objects;
        MonitorPool m_monitors;
        ControllerList m_controllers;
        WorkQueue<void> m_work_queue;


        void pre_update( UpdateArgs... update_args );

        template< class ObjectCreation >
        Monitor& create_monitored_object( ObjectCreation&& object_creation );

        template< class CreationStatement >
        future<Handle> create_future_monitored( CreationStatement&& creation_statement );

        void create_object() { m_objects.emplace_back(); }
        template<class... InitData>
        void create_object( InitData&&... init_data ){ m_objects.emplace_back( std::forward<InitData>(init_data)... ); }

    };


    template< class ObjectType, class... UpdateArgs >
    MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::MonitoredObjectPool( size_t reserve_size )
    {
        m_objects.reserve( reserve_size );
        m_monitors.reserve( reserve_size );
    }


    template< class ObjectType, class... UpdateArgs >
    template< class ObjectCreation >
    auto MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create_monitored_object( ObjectCreation&& object_creation  ) -> Monitor&
    {
        object_creation();
        auto& object = m_objects.back();

        m_monitors.emplace_back( object );
        auto& monitor = m_monitors.back();

        return monitor;
    }


    template< class ObjectType, class... UpdateArgs >
    template< class CreationStatement >
    auto MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create_future_monitored( CreationStatement&& creation_statement ) -> future<Handle>
    {
        return async_exec( m_work_queue, [this, creation_statement] {
            auto& monitor = create_monitored_object( creation_statement );
            Handle handle( monitor, this->shared_from_this() );
            UCX_ASSERT_TRUE( is_valid(handle) );
            return handle;
        });
    }


    template< class ObjectType, class... UpdateArgs >
    auto MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create() -> future<Handle>
    {
        return create_future_monitored( [this]{ create_object(); });
    }


    template< class ObjectType, class... UpdateArgs >
    template< class... InitData >
    auto MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create( InitData... init_data ) -> future<Handle>
    {
        return create_future_monitored( [this, init_data...]{ create_object( init_data... ); } );
    }

    template< class ObjectType, class... UpdateArgs >
    template< class WorkOnMonitor >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create_then( WorkOnMonitor&& work )
    {
        m_work_queue.push( [this, work]
        {
            auto& monitor = create_monitored_object( [&]{ create_object(); } );
            Handle handle( monitor, this->shared_from_this() );
            UCX_ASSERT_TRUE( is_valid(handle) );
            work( std::move( handle ) );
        });
    }


    template< class ObjectType, class... UpdateArgs >
    template< class WorkOnMonitor, class... InitData >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create_then(  WorkOnMonitor&& work, InitData... init_data )
    {
        m_work_queue.push( [this, work, init_data...]
        {
            auto& monitor = create_monitored_object( [&]{ create_object( init_data... ); } );
            Handle handle( monitor, this->shared_from_this() );
            UCX_ASSERT_TRUE( is_valid(handle) );
            work( std::move( handle ) );
        });
    }


    template< class ObjectType, class... UpdateArgs >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create_then_set( Handle& handle )
    {
        auto* ptr_to_handle = &handle;
        create_then( [=]( Handle&& new_handle ){
            *ptr_to_handle = std::move(new_handle);
        } );
    }


    template< class ObjectType, class... UpdateArgs >
    template< class... InitData >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create_then_set( Handle& handle, InitData&&... init_data )
    {
        auto* ptr_to_handle = &handle;
        create_then( [=]( Handle&& new_handle ){
                *ptr_to_handle = std::move(new_handle);
            }
        , std::forward<InitData>(init_data)...
        );
    }

    template< class ObjectType, class... UpdateArgs >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create_standalone()
    {
        m_work_queue.push( [&]{
            create_monitored_object([&]{ create_object(); } );
        });
    }


    template< class ObjectType, class... UpdateArgs >
    template< class... InitData >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::create_standalone( InitData... init_data )
    {
        m_work_queue.push( [this, init_data...]{
            create_monitored_object([&]{ create_object( init_data... ); } );
        });
    }


    template< class ObjectType, class... UpdateArgs >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::destroy( Monitor& monitor )
    {
        auto* monitor_ptr = &monitor;
        ObjectType* object_ptr = &monitor.unsafe_get();
        // We plan the deletion of the object and it's monitor for the next update.
        m_work_queue.push( [this, monitor, monitor_ptr, object_ptr] {
            // First, destroy the monitor.
            auto monitor_it = std::find_if( begin(m_monitors), end(m_monitors)
                                            , [=]( Monitor& m ) { return &m == monitor_ptr; } );
            UCX_ASSERT( monitor_it != end(m_monitors), "Trying to erase a monitor not found in the pool." );
            m_monitors.erase( monitor_it );

            // Then destroy the object itself.
            auto object_it = std::find_if( begin(m_objects), end(m_objects)
                , [=]( ObjectType& o ){ return &o == object_ptr; } );
            UCX_ASSERT( object_it != end(m_objects), "Trying to erase a monitor not found in the pool." );
            m_objects.erase( object_it );

        });
        /* Note that returning this function will not immediately apply the
            deletion of the object, but it will reset the pointer to null,
            making both the monitor and it's object inaccessible.
        */
    }


    template< class ObjectType, class... UpdateArgs >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::pre_update( UpdateArgs... update_args )
    {
        m_work_queue.execute();

        for( auto& monitor : m_monitors )
            monitor.execute_work( update_args... );
    }


    template< class ObjectType, class... UpdateArgs >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::update( UpdateArgs... update_args )
    {
        pre_update( update_args... );

        m_controllers.update_batch( begin(m_objects), end(m_objects), update_args... );

    }


    template< class ObjectType, class... UpdateArgs >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::add_controller( Controller& controller )
    {
        m_work_queue.push( [&,this] {
            m_controllers.add( controller );
        } );
    }


    template< class ObjectType, class... UpdateArgs >
    void MonitoredObjectPool<ObjectType,UpdateArgs...>
        ::remove_controller( Controller& controller )
    {
        m_work_queue.push( [&, this] {
            m_controllers.remove( controller );
        } );
    }

}}


