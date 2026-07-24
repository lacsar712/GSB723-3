const CreditAPI = (() => {
  const INITIAL_SCORE = 100;
  const MIN_ACCEPT_SCORE = 60;

  function getLevel(score) {
    if (score >= 90) return { level: 0, label: '优', color: '#10b981' };
    if (score >= 75) return { level: 1, label: '良', color: '#3b82f6' };
    if (score >= 60) return { level: 2, label: '中', color: '#f59e0b' };
    return { level: 3, label: '差', color: '#ef4444' };
  }

  function canAcceptOrders(score) {
    return score >= MIN_ACCEPT_SCORE;
  }

  function computeNewScore(oldScore, rating) {
    const s = oldScore * 0.8 + rating * 20 * 0.2;
    return Math.round(s);
  }

  function formatTime(ts) {
    if (!ts) return '';
    const d = new Date(ts * 1000);
    const pad = n => String(n).padStart(2, '0');
    return `${d.getFullYear()}-${pad(d.getMonth() + 1)}-${pad(d.getDate())} ${pad(d.getHours())}:${pad(d.getMinutes())}`;
  }

  function maskName(name) {
    if (!name) return '**';
    if (name.length <= 1) return name + '*';
    return name[0] + (name.length > 2 ? '**' : '*');
  }

  function statusLabel(s) {
    const map = {
      pending: '待裁决',
      established: '已成立',
      rejected: '已驳回'
    };
    return map[s] || s;
  }

  function orderStatusLabel(s) {
    const map = {
      pending: '待接单', accepted: '进行中', delivered: '待收货',
      completed: '已完成', cancelled: '已撤回', refunded: '已退款'
    };
    return map[s] || s;
  }

  async function fetchCredit(username) {
    const resp = await fetch(`/api/credit?username=${encodeURIComponent(username)}`);
    return resp.json();
  }

  async function fetchRatings(username, direction = 'received', limit = 10) {
    const resp = await fetch(`/api/ratings?username=${encodeURIComponent(username)}&direction=${direction}&limit=${limit}`);
    return resp.json();
  }

  async function submitRating(payload) {
    const resp = await fetch('/api/ratings', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    return resp.json();
  }

  async function fetchDisputes(username) {
    const url = username
      ? `/api/disputes?username=${encodeURIComponent(username)}`
      : '/api/disputes';
    const resp = await fetch(url);
    return resp.json();
  }

  async function fetchDisputeDetail(id) {
    const resp = await fetch(`/api/disputes?id=${id}`);
    return resp.json();
  }

  async function createDispute(payload) {
    const resp = await fetch('/api/disputes', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    return resp.json();
  }

  async function checkDisputes() {
    const resp = await fetch('/api/disputes/check', { method: 'POST' });
    return resp.json();
  }

  function getCurrentUser() {
    try {
      return JSON.parse(localStorage.getItem('user'));
    } catch (e) { return null; }
  }

  function setCurrentUser(u) {
    if (u) localStorage.setItem('user', JSON.stringify(u));
    else localStorage.removeItem('user');
  }

  return {
    INITIAL_SCORE, MIN_ACCEPT_SCORE,
    getLevel, canAcceptOrders, computeNewScore,
    formatTime, maskName, statusLabel, orderStatusLabel,
    fetchCredit, fetchRatings, submitRating,
    fetchDisputes, fetchDisputeDetail, createDispute, checkDisputes,
    getCurrentUser, setCurrentUser
  };
})();
