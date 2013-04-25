// Copyright (C) by Ashton Mason. See LICENSE.txt for licensing information.


#include <Theron/Actor.h>
#include <Theron/Assert.h>
#include <Theron/Defines.h>

#include <Theron/Detail/Handlers/FallbackHandlerCollection.h>
#include <Theron/Detail/Mailboxes/Mailbox.h>
#include <Theron/Detail/Scheduler/MailboxProcessor.h>
#include <Theron/Detail/Messages/IMessage.h>
#include <Theron/Detail/Messages/MessageCreator.h>


namespace Theron
{
namespace Detail
{


void MailboxProcessor::Process(WorkerContext *const workerContext, Mailbox *const mailbox)
{
    // Load the context data from the worker thread's mailbox context.
    // Actors that need more processing are always pushed onto the worker thread's local queue.
    MailboxContext *const context(&workerContext->mMailboxContext);
    FallbackHandlerCollection *const fallbackHandlers(context->mFallbackHandlers);
    IAllocator *const messageAllocator(context->mMessageAllocator);

    THERON_ASSERT(fallbackHandlers);
    THERON_ASSERT(messageAllocator);
    THERON_ASSERT(!mailbox->Empty());

    // Pin the mailbox and get the registered actor and the first queued message.
    // At this point the mailbox shouldn't be enqueued in any other work items,
    // even if it contains more than one unprocessed message. This ensures that
    // each mailbox is only processed by one worker thread at a time.
    mailbox->Lock();
    mailbox->Pin();
    Actor *const actor(mailbox->GetActor());
    IMessage *const message(mailbox->Front());
    mailbox->Unlock();

    // If an entity is registered at the mailbox then process it.
    if (actor)
    {
        // Store a pointer to the context data for this thread in the actor.
        // We'll need it to send messages if any of the registered handlers
        // call Actor::Send, but we can't pass it through from here because
        // the handlers are user code.
        THERON_ASSERT(actor->mMailboxContext == 0);
        actor->mMailboxContext = context;

        actor->ProcessMessage(fallbackHandlers, message);

        // Zero the context pointer, in case it's next accessed by a non-worker thread.
        THERON_ASSERT(actor->mMailboxContext == context);
        actor->mMailboxContext = 0;
    }

    if (actor == 0 && fallbackHandlers)
    {
        fallbackHandlers->Handle(message);
    }

    mailbox->Lock();

    // Unpin the mailbox, allowing the registered actor to be changed by other threads.
    mailbox->Unpin();

    // Pop the message we just processed from the mailbox, then check whether the
    // mailbox is now empty, and reschedule the mailbox if it's not.
    // The locking of the mailbox here and in the main scheduling ensures that
    // mailboxes are always enqueued if they have unprocessed messages, but at most
    // once at any time.
    mailbox->Pop();
    if (!mailbox->Empty())
    {
        context->mScheduler->Schedule(context->mQueueContext, mailbox);
    }

    mailbox->Unlock();

    // Destroy the message, but only after we've popped it from the queue.
    MessageCreator::Destroy(messageAllocator, message);
}


} // namespace Detail
} // namespace Theron


