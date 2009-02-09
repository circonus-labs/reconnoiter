/**
 * @author alexander.farkas
 * @version 1.21 
 */
(function($){
    $.extend({
        manageAjax: function(o){
            o = $.extend({
                manageType: 'normal',
                maxReq: 0,
                blockSameRequest: false,
				global: true
            }, o);
            return new $.ajaxManager(o);
        },
        ajaxManager: function(o){
            this.opt = o;
            this.queue = [];
        }
    });
    $.extend($.ajaxManager.prototype, {
        add: function(o){
			var quLen = this.queue.length, s = this.opt, q = this.queue, self = this, i, j;
			o = $.extend({}, s, o);
            var cD = (o.data && typeof o.data != "string") ? $.param(o.data) : o.data;
            if (s.blockSameRequest) {
                var toPrevent = false;
                for (i = 0; i < quLen; i++) {
                    if (q[i] && q[i].data === cD && q[i].url === o.url && q[i].type === o.type) {
                        toPrevent = true;
                        break;
                    }
                }
                if (toPrevent) {
                    return false;
                }
            }
            q[quLen] = {
                fnError: o.error,
                fnSuccess: o.success,
                fnComplete: o.complete,
                fnAbort: o.abort,
                error: [],
                success: [],
                complete: [],
                done: false,
                queued: false,
                data: cD,
                url: o.url,
                type: o.type,
                xhr: null
            };
            
            o.error = function(){
                if (q[quLen]) {
                    q[quLen].error = arguments;
                }
            };
            o.success = function(){
                if (q[quLen]) {
                    q[quLen].success = arguments;
                }
            };
            o.abort = function(){
                if (q[quLen]) {
                    q[quLen].abort = arguments;
                }
            };
			
            function startCallbacks(num, opts){
                if (q[num].fnError && q[num].error.length) {
                    q[num].fnError.apply(opts || $, q[num].error);
                }
				
                if (q[num].fnSuccess) {
                   
					q[num].fnSuccess.apply(opts || $, q[num].success);
                }
                if (q[num].fnComplete) {
                    q[num].fnComplete.apply(opts || $, q[num].complete);
                }
                self.abort(num, true);
            }
            
            o.complete = function(){
                if (!q[quLen]) {
                    return;
                }
                q[quLen].complete = arguments;
                q[quLen].done = true;
                switch (s.manageType) {
                    case 'sync':
                        if (quLen === 0 || !q[quLen - 1]) {
                            var curQLen = q.length;
                            for (i = quLen; i < curQLen; i++) {
                                if (q[i]) {
                                    if (q[i].done) {
										startCallbacks(i, this);
                                    }
                                    else {
                                        break;
                                    }
                                }
                                
                            }
                        }
                        break;
                    case 'queue':
                        if (quLen === 0 || !q[quLen - 1]) {
                            var curQLen = q.length;
                            for (i = 0, j = 0; i < curQLen; i++) {
                                if (q[i] && q[i].queued) {
                                    q[i].xhr = jQuery.ajax(q[i].xhr);
                                    q[i].queued = false;
                                    break;
                                }
                            }
                        }
                        startCallbacks(quLen, this);
                        break;
                    case 'abortOld':
                        startCallbacks(quLen, this);
                        for (i = quLen; i >= 0; i--) {
                            if (q[i]) {
                                self.abort(i);
                            }
                        }
                        break;
                    default:
                        startCallbacks(quLen, this);
                        break;
                }
            };
            
            if (s.maxReq) {
                if (s.manageType != 'queue') {
                    for (i = quLen, j = 0; i >= 0; i--) {
                        if (j >= s.maxReq) {
                            this.abort(i);
                        }
                        if (q[i]) {
                            j++;
                        }
                    }
                }
                else {
                    for (i = 0, j = 0; i <= quLen && !q[quLen].queued; i++) {
                        if (q[i] && !q[i].queued) 
                            j++;
                        if (j > s.maxReq) 
                            q[quLen].queued = true;
                    }
                }
            }
            q[quLen].xhr = (q[quLen].queued) ? o : jQuery.ajax(o);
            return quLen;
        },
        cleanUp: function(){
            this.queue = [];
        },
        abort: function(num, completed){
            var qLen = this.queue.length, s = this.opt, q = this.queue, self = this, i;
            function del(num){
                if (!q[num]) {
                    return;
                }
                ((!completed && q[num].fnAbort) && q[num].fnAbort.apply($, [num]));
                if (!q[num]) {
                    return;
                }
                if (q[num].xhr) {
                    if (typeof q[num].xhr.abort != 'undefined') {
                        q[num].xhr.abort();
                    }
                    if (typeof q[num].xhr.close != 'undefined') {
                        q[num].xhr.close();
                    }
                    q[num].xhr = null;
                }
				// Handle the global AJAX counter
			
				if ( s.global && $.active && ! --$.active){
					$.event.trigger( "ajaxStop" );
				}
                q[num] = null;
            }
            if (!num && num !== 0) {
                for (i = 0; i < qLen; i++) {
                    del(i);
                }
                this.cleanUp();
            }
            else {
                del(num);
                var allowCleaning = true;
                for (i = qLen; i >= 0; i--) {
                    if (q[i]) {
                        allowCleaning = false;
                        break;
                    }
                }
                if (allowCleaning) {
                    this.cleanUp();
                }
            }
        }
    });
})(jQuery);
