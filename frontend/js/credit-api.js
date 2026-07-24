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

    async function respondDispute(id, user, response) {
        const resp = await fetch('/api/dispute/respond', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ id, user, response })
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

    function statusLabel(s) { return STATUS_LABELS[s] || s; }
    function disputeStatusLabel(s) { return DISPUTE_STATUS_LABELS[s] || s; }

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
        createDispute,
        respondDispute,
        statusLabel,
        disputeStatusLabel,
        canFileDispute,
        canRateOrder,
        ratingTarget,
        maskName,
        formatTime
    };
})(window);
