/* credit-api.js — API 请求 + 业务规则 + 状态计算（不含 DOM 渲染） */
(function (global) {
    'use strict';

    const INITIAL_CREDIT = 100;
    const MIN_ACCEPT_CREDIT = 60;

    function gradeOf(score) {
        if (score >= 90) return '优';
        if (score >= 75) return '良';
        if (score >= 60) return '中';
        return '差';
    }

    function canAccept(score) { return score >= MIN_ACCEPT_CREDIT; }

    function computeNextScore(oldScore, rating) {
        const next = oldScore * 0.8 + rating * 20 * 0.2;
        return Math.max(0, Math.min(100, Math.round(next)));
    }

    function currentUser() {
        try { return JSON.parse(localStorage.getItem('user')) || null; }
        catch (e) { return null; }
    }

    function saveUser(u) {
        if (!u) return;
        localStorage.setItem('user', JSON.stringify(u));
    }

    async function jsonOrThrow(resp) {
        let data = null;
        try { data = await resp.json(); } catch (e) { data = null; }
        if (!resp.ok) {
            const msg = (data && data.message) || ('请求失败 (' + resp.status + ')');
            const err = new Error(msg);
            err.status = resp.status;
            err.data = data;
            throw err;
        }
        return data;
    }

    async function getCredit(username) {
        const resp = await fetch('/api/credit?user=' + encodeURIComponent(username));
        return jsonOrThrow(resp);
    }

    async function getCreditsMap() {
        const resp = await fetch('/api/credits');
        const arr = await jsonOrThrow(resp);
        const map = {};
        (arr || []).forEach(u => { map[u.username] = u; });
        return map;
    }

    async function getRatings(username, direction) {
        const u = encodeURIComponent(username);
        const d = encodeURIComponent(direction || 'received');
        const resp = await fetch('/api/ratings?user=' + u + '&filter=' + d + '&limit=10');
        return jsonOrThrow(resp);
    }

    async function submitRating(orderId, from, score, comment) {
        const resp = await fetch('/api/ratings', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ orderId, from, score, comment: comment || '' })
        });
        return jsonOrThrow(resp);
    }

    async function getDisputes(username) {
        const u = username ? '?user=' + encodeURIComponent(username) : '';
        const resp = await fetch('/api/disputes' + u);
        return jsonOrThrow(resp);
    }

    async function getEvents(username, limit) {
        const u = encodeURIComponent(username || '');
        const lim = limit || 20;
        const resp = await fetch('/api/events?user=' + u + '&limit=' + lim);
        return jsonOrThrow(resp);
    }

    async function getDispute(id) {
        const resp = await fetch('/api/disputes?id=' + encodeURIComponent(id));
        return jsonOrThrow(resp);
    }

    async function createDispute(orderId, initiator, reason) {
        const resp = await fetch('/api/disputes', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ orderId, initiator, reason })
        });
        return jsonOrThrow(resp);
    }

    async function respondDispute(id, user, response, evidence) {
        const payload = { id, user, response: response || '' };
        if (evidence && evidence.type) payload.evidenceType = evidence.type;
        if (evidence && evidence.desc) payload.evidenceDesc = evidence.desc;
        const resp = await fetch('/api/dispute/respond', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify(payload)
        });
        return jsonOrThrow(resp);
    }

    const STATUS_LABELS = {
        pending: '待接单',
        accepted: '进行中',
        delivered: '待收货',
        completed: '已完成',
        cancelled: '已撤回',
        disputed: '纠纷中',
        refunded: '已退款'
    };

    const DISPUTE_STATUS_LABELS = {
        pending: '待裁决',
        upheld: '已成立',
        rejected: '已驳回'
    };

    const EVENT_META = {
        rating_received: { label: '收到评价', icon: 'fa-star', tone: 'rating' },
        rating_given:    { label: '给出评价', icon: 'fa-pen-to-square', tone: 'rating' },
        dispute_filed:   { label: '发起纠纷', icon: 'fa-gavel', tone: 'dispute' },
        dispute_upheld:  { label: '纠纷成立', icon: 'fa-flag-checkered', tone: 'dispute' },
        dispute_rejected:{ label: '纠纷驳回', icon: 'fa-circle-check', tone: 'dispute' },
        credit_change:   { label: '信用分变更', icon: 'fa-shield-halved', tone: 'credit' }
    };

    function statusLabel(s) { return STATUS_LABELS[s] || s; }
    function disputeStatusLabel(s) { return DISPUTE_STATUS_LABELS[s] || s; }
    function eventMeta(type) {
        return EVENT_META[type] || { label: type || '事件', icon: 'fa-circle-info', tone: 'neutral' };
    }
    function eventTypeLabel(type) { return eventMeta(type).label; }

    const EVIDENCE_TYPES = [
        { value: '', label: '无（可选）' },
        { value: 'screenshot', label: '截图说明' },
        { value: 'chat', label: '聊天记录' },
        { value: 'other', label: '其他' }
    ];

    function evidenceTypeLabel(t) {
        const hit = EVIDENCE_TYPES.find(x => x.value === t);
        return hit ? hit.label : (t || '无');
    }

    function canFileDispute(order, username) {
        if (!order || !username) return false;
        if (order.frozen) return false;
        if (order.status !== 'accepted' && order.status !== 'delivered') return false;
        if (order.creator !== username && order.worker !== username) return false;
        return true;
    }

    function canRateOrder(order, username, myRatings) {
        if (!order || !username) return false;
        if (order.status !== 'completed') return false;
        if (order.creator !== username && order.worker !== username) return false;
        if (!myRatings) return true;
        return !myRatings.some(r => r.orderId === order.id && r.fromUser === username);
    }

    function ratingTarget(order, username) {
        if (!order) return '';
        if (order.creator === username) return order.worker;
        if (order.worker === username) return order.creator;
        return '';
    }

    function maskName(name) {
        if (!name) return '同学***';
        if (name.length <= 1) return name + '***';
        return name.charAt(0) + '***';
    }

    function formatTime(ts) {
        if (!ts) return '';
        const d = new Date(ts * 1000);
        if (isNaN(d.getTime())) return '';
        const pad = n => String(n).padStart(2, '0');
        return d.getFullYear() + '-' + pad(d.getMonth() + 1) + '-' + pad(d.getDate())
            + ' ' + pad(d.getHours()) + ':' + pad(d.getMinutes());
    }

    global.CreditAPI = {
        INITIAL_CREDIT,
        MIN_ACCEPT_CREDIT,
        gradeOf,
        canAccept,
        computeNextScore,
        currentUser,
        saveUser,
        getCredit,
        getCreditsMap,
        getRatings,
        submitRating,
        getDisputes,
        getDispute,
        getEvents,
        createDispute,
        respondDispute,
        statusLabel,
        disputeStatusLabel,
        eventMeta,
        eventTypeLabel,
        EVIDENCE_TYPES,
        evidenceTypeLabel,
        canFileDispute,
        canRateOrder,
        ratingTarget,
        maskName,
        formatTime
    };
})(window);
