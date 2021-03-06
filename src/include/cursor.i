/*-
 * Copyright (c) 2008-2012 WiredTiger, Inc.
 *	All rights reserved.
 *
 * See the file LICENSE for redistribution information.
 */

/*
 * __cursor_search_clear --
 *	Reset the cursor's state for a search.
 */
static inline void
__cursor_search_clear(WT_CURSOR_BTREE *cbt)
{
	/* Our caller should have released any page held by this cursor. */
	cbt->page = NULL;
	cbt->slot = UINT32_MAX;			/* Fail big */

	cbt->ins_head = NULL;
	cbt->ins = NULL;
	cbt->ins_stack[0] = NULL;
	/* We don't bother clearing the insert stack, that's more expensive. */

	cbt->recno = 0;				/* Illegal value */
	cbt->write_gen = 0;

	cbt->compare = 2;			/* Illegal value */

	cbt->cip_saved = NULL;
	cbt->rip_saved = NULL;

	cbt->flags = 0;
}

/*
 * __cursor_func_init --
 *	Reset the cursor's state for a new call.
 */
static inline void
__cursor_func_init(WT_CURSOR_BTREE *cbt, int page_release)
{
	WT_CURSOR *cursor;
	WT_SESSION_IMPL *session;

	cursor = &cbt->iface;
	session = (WT_SESSION_IMPL *)cursor->session;

	/* Optionally release any page references we're holding. */
	if (page_release && cbt->page != NULL) {
		__wt_page_release(session, cbt->page);
		cbt->page = NULL;
	}

	/* Reset the returned key/value state. */
	F_CLR(cursor, WT_CURSTD_KEY_SET | WT_CURSTD_VALUE_SET);
}

/*
 * __cursor_func_resolve --
 *	Resolve the cursor's state for return.
 */
static inline void
__cursor_func_resolve(WT_CURSOR_BTREE *cbt, int ret)
{
	WT_CURSOR *cursor;

	cursor = &cbt->iface;

	/*
	 * On success, we're returning a key/value pair, and can iterate.
	 * On error, we're not returning anything, we can't iterate, and
	 * we should release any page references we're holding.
	 */
	if (ret == 0)
		F_SET(cursor, WT_CURSTD_KEY_SET | WT_CURSTD_VALUE_SET);
	else {
		__cursor_func_init(cbt, 1);
		__cursor_search_clear(cbt);
	}
}

/*
 * __cursor_row_slot_return --
 *	Return a WT_ROW slot's K/V pair.
 */
static inline int
__cursor_row_slot_return(WT_CURSOR_BTREE *cbt, WT_ROW *rip)
{
	WT_BTREE *btree;
	WT_ITEM *kb, *vb;
	WT_CELL *cell;
	WT_CELL_UNPACK *unpack, _unpack;
	WT_IKEY *ikey;
	WT_SESSION_IMPL *session;
	WT_UPDATE *upd;
	void *key;

	session = (WT_SESSION_IMPL *)cbt->iface.session;
	btree = session->btree;
	unpack = &_unpack;

	kb = &cbt->iface.key;
	vb = &cbt->iface.value;

	/*
	 * Return the WT_ROW slot's K/V pair.
	 */

	key = WT_ROW_KEY_COPY(rip);
	/*
	 * Key copied.
	 *
	 * If another thread instantiated the key, we don't have any work to do.
	 * Figure this out using the key's value:
	 *
	 * If the key points off-page, the key has been instantiated, just use
	 * it.
	 *
	 * If the key points on-page, we have a copy of a WT_CELL value that can
	 * be processed, regardless of what any other thread is doing.
	 */
	if (__wt_off_page(cbt->page, key)) {
		ikey = key;
		kb->data = WT_IKEY_DATA(ikey);
		kb->size = ikey->size;
	} else {
		/*
		 * If the key is simple and on-page, just reference it.
		 * Else, if the key is simple, prefix-compressed and on-page,
		 * and we have the previous expanded key in the cursor buffer,
		 * build the key quickly.
		 * Else, instantiate the key and do it all  the hard way.
		 */
		if (btree->huffman_key != NULL)
			goto slow;
		__wt_cell_unpack(key, unpack);
		if (unpack->type == WT_CELL_KEY && unpack->prefix == 0) {
			kb->data = cbt->tmp.data = unpack->data;
			kb->size = cbt->tmp.size = unpack->size;
			cbt->rip_saved = rip;
		} else if (unpack->type == WT_CELL_KEY &&
		    cbt->rip_saved != NULL && cbt->rip_saved == rip - 1) {
			/*
			 * If we previously built a prefix-compressed key in the
			 * temporary buffer, the WT_ITEM->data field will be the
			 * same as the WT_ITEM->mem field: grow the buffer if
			 * necessary and copy the suffix into place.  If we
			 * previously pointed the temporary buffer at an on-page
			 * key, the WT_ITEM->data field will not be the same as
			 * the WT_ITEM->mem field: grow the buffer if necessary,
			 * copy the prefix into place, and then re-point the
			 * WT_ITEM->data field to the newly constructed memory.
			 */
			WT_RET(__wt_buf_grow(
			    session, &cbt->tmp, unpack->prefix + unpack->size));
			if (cbt->tmp.data != cbt->tmp.mem) {
				memcpy((uint8_t *)cbt->tmp.mem,
				    cbt->tmp.data, unpack->prefix);
				cbt->tmp.data = cbt->tmp.mem;
			}
			memcpy((uint8_t *)cbt->tmp.data +
			    unpack->prefix, unpack->data, unpack->size);
			cbt->tmp.size = unpack->prefix + unpack->size;
			kb->data = cbt->tmp.data;
			kb->size = cbt->tmp.size;
			cbt->rip_saved = rip;
		} else
slow:			WT_RET(__wt_row_key(session, cbt->page, rip, kb));
	}

	/*
	 * If the item was ever modified, use the WT_UPDATE data.
	 * Else, check for empty data.
	 * Else, use the value from the original disk image.
	 */
	if ((upd = WT_ROW_UPDATE(cbt->page, rip)) != NULL) {
		vb->data = WT_UPDATE_DATA(upd);
		vb->size = upd->size;
	} else if ((cell = __wt_row_value(cbt->page, rip)) == NULL) {
		vb->data = "";
		vb->size = 0;
	} else {
		__wt_cell_unpack(cell, unpack);
		if (unpack->type == WT_CELL_VALUE &&
		    btree->huffman_value == NULL) {
			vb->data = unpack->data;
			vb->size = unpack->size;
		} else
			WT_RET(__wt_cell_unpack_copy(session, unpack, vb));
	}

	return (0);
}
