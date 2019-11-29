/*
 * elevator c-look
 */
#include <linux/blkdev.h>
#include <linux/elevator.h>
#include <linux/bio.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/init.h>

//Position of the read+write head
sector_t head = 0L;

struct clook_data {
	struct list_head queue;
};

/*
 * Reports the instruction (read or write) and sector of the added request.
 */
static void show_added_request(struct request *rq) {
	unsigned long curr_instruction = rq_data_dir(rq);
	sector_t curr_sector = blk_rq_pos(rq);
	char curr_instruction_str[10]; 	

	if (curr_instruction == READ) {	
		strcpy(curr_instruction_str, "R");
	} else if (curr_instruction == WRITE) {
		strcpy(curr_instruction_str, "W");
	}
	printk("[CLOOK] add %s %20lu\n", curr_instruction_str, curr_sector);	
}

/*
 * Reports the instruction (read or write) and sector of the dispatched request.
 */
static void show_dispatched_request(struct request *rq) {	
	unsigned long curr_instruction = rq_data_dir(rq);
	sector_t curr_sector = blk_rq_pos(rq);
	char curr_instruction_str[10]; 	

	if (curr_instruction == READ) {	
		strcpy(curr_instruction_str, "R");
	} else if (curr_instruction == WRITE) {
		strcpy(curr_instruction_str, "W");
	}
	printk("[CLOOK] dsp %s %20lu\n", curr_instruction_str, curr_sector);
}

static void clook_merged_requests(struct request_queue *q, struct request *rq,
				 struct request *next)
{
	list_del_init(&next->queuelist);
}

static int clook_dispatch(struct request_queue *q, int force)
{
	struct clook_data *cd = q->elevator->elevator_data;

	if (!list_empty(&cd->queue)) {
		struct request *rq;
		rq = list_entry(cd->queue.next, struct request, queuelist);
		list_del_init(&rq->queuelist);
		//Show direction and sector of dispatched request
		show_dispatched_request(rq);
		//Sector of dispatched request is also the sector position of the
		//read+write head, so save it to set up the request list
		head = blk_rq_pos(rq);
		elv_dispatch_sort(q, rq);
		return 1;
	}
	return 0;
}

static void clook_add_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *cd = q->elevator->elevator_data;
 	struct request *curr_request = NULL;
	sector_t sector = blk_rq_pos(rq);
	sector_t curr_sector = 0L;
	struct list_head *itr = NULL;
	
	//Show direction and sector of added request
	show_added_request(rq);	

	//list_add_tail(&rq->queuelist, &cd->queue);	
	//return;
	
	if (list_empty(&cd->queue)) {
		list_add_tail(&rq->queuelist, &cd->queue);	
		return;
	}
	
	if (sector >= head) { //high priority
		list_for_each(itr, &cd->queue) {
			curr_request = list_entry(itr, struct request, queuelist);
			curr_sector = blk_rq_pos(curr_request);
			if (curr_sector < head || sector <= curr_sector) {
				list_add_tail(&rq->queuelist, itr);
				return;		
			} else if (list_is_last(itr, &cd->queue)) {
				/*
				 * TODO: FILL THIS OUT
				 * 
				 */
				list_add(&rq->queuelist, itr);
				return;
			} else {
				/*
				 * This request is in the high priority area but
				 * is not in the right insertion spot, so skip
				 */
				;
			}
		}
	} else { //low priority
		list_for_each(itr, &cd->queue) {
			curr_request = list_entry(itr, struct request, queuelist);
			curr_sector = blk_rq_pos(curr_request);
			if (curr_sector >= head) {
				/* 
				 * Keep skipping until first low priority request
				 * is found. This is to ensure that high priority
				 * requests remain in the front of the list.
				 *
				 * If no low priority requests are found in the list, 
				 * then add this low priority request right after
				 * the last high priority request.
				 */
				if (list_is_last(itr, &cd->queue)) {
					list_add(&rq->queuelist, itr);
					return;
				}
			} else if (sector <= curr_sector) {
				list_add_tail(&rq->queuelist, itr);
				return;				
			} else if (list_is_last(itr, &cd->queue)) {
				list_add(&rq->queuelist, itr);
				return;
			} else {
				;
			}
		}
	}
}

static struct request *
clook_former_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *cd = q->elevator->elevator_data;

	if (rq->queuelist.prev == &cd->queue)
		return NULL;
	return list_entry(rq->queuelist.prev, struct request, queuelist);
}

static struct request *
clook_latter_request(struct request_queue *q, struct request *rq)
{
	struct clook_data *cd = q->elevator->elevator_data;

	if (rq->queuelist.next == &cd->queue)
		return NULL;
	return list_entry(rq->queuelist.next, struct request, queuelist);
}

static int clook_init_queue(struct request_queue *q)
{
	struct clook_data *cd;

	cd = kmalloc_node(sizeof(*cd), GFP_KERNEL, q->node);
	if (!cd)
		return -ENOMEM;

	INIT_LIST_HEAD(&cd->queue);
	q->elevator->elevator_data = cd;
	return 0;
}

static void clook_exit_queue(struct elevator_queue *e)
{
	struct clook_data *cd = e->elevator_data;

	BUG_ON(!list_empty(&cd->queue));
	kfree(cd);
}

static struct elevator_type elevator_clook = {
	.ops = {
		.elevator_merge_req_fn		= clook_merged_requests,
		.elevator_dispatch_fn		= clook_dispatch,
		.elevator_add_req_fn		= clook_add_request,
		.elevator_former_req_fn		= clook_former_request,
		.elevator_latter_req_fn		= clook_latter_request,
		.elevator_init_fn		= clook_init_queue,
		.elevator_exit_fn		= clook_exit_queue,
	},
	.elevator_name = "clook",
	.elevator_owner = THIS_MODULE,
};

static int __init clook_init(void)
{
	return elv_register(&elevator_clook);
}

static void __exit clook_exit(void)
{
	elv_unregister(&elevator_clook);
}

module_init(clook_init);
module_exit(clook_exit);


MODULE_AUTHOR("Jens Axboe, Marvin Torres");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("C-LOOK IO scheduler");