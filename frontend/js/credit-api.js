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

  function eventTypeMeta(type) {
    const map = {
      rating_received: { label: '收到评价', icon: 'fa-star', color: '#fbbf24' },
      rating_given: { label: '给出评价', icon: 'fa-pen', color: '#6366f1' },
      dispute_created: { label: '发起纠纷', icon: 'fa-gavel', color: '#f43f5e' },
      dispute_established: { label: '纠纷成立', icon: 'fa-exclamation-circle', color: '#dc2626' },
      dispute_rejected: { label: '纠纷驳回', icon: 'fa-check-circle', color: '#10b981' },
      statement_submitted: { label: '补充说明', icon: 'fa-comment-dots', color: '#0ea5e9' },
      score_changed: { label: '信用分变更', icon: 'fa-chart-line', color: '#8b5cf6' }
    };
    return map[type] || { label: type, icon: 'fa-circle', color: '#64748b' };
  }

  const EVIDENCE_TYPES = [
    { value: 'screenshot', label: '截图说明' },
    { value: 'chat', label: '聊天记录' },
    { value: 'other', label: '其他' },
    { value: '', label: '无' }
  ];

  const HALL_FILTERS = {
    ALL: 'all',
    GOOD_ONLY: 'good',
    LOW_CREDIT: 'low'
  };

  function isGoodCredit(score) {
    return score >= 75;
  }

  function isLowCredit(score) {
    return score < 60;
  }

  function isWarningCredit(score) {
    return score < 75 && score >= 60;
  }

  function groupOrdersForHall(orders, currentUsername) {
    const pending = orders.filter(o => o.status === 'pending' && o.creator !== currentUsername);
    const normal = [];
    const low = [];
    pending.forEach(o => {
      if (isLowCredit(o.creatorCredit || 100)) {
        low.push(o);
      } else {
        normal.push(o);
      }
    });
    return { normal, low };
  }

  function filterOrders(orders, filterType) {
    if (filterType === HALL_FILTERS.GOOD_ONLY) {
      return orders.filter(o => isGoodCredit(o.creatorCredit || 100));
    }
    if (filterType === HALL_FILTERS.LOW_CREDIT) {
      return orders.filter(o => isLowCredit(o.creatorCredit || 100));
    }
    return orders;
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

  async function fetchEvents(username, limit = 30) {
    const resp = await fetch(`/api/events?username=${encodeURIComponent(username)}&limit=${limit}`);
    return resp.json();
  }

  async function submitDisputeStatement(payload) {
    const resp = await fetch('/api/disputes/statement', {
      method: 'POST',
      headers: { 'Content-Type': 'application/json' },
      body: JSON.stringify(payload)
    });
    return resp.json();
  }

  function evidenceTypeLabel(value) {
    const found = EVIDENCE_TYPES.find(e => e.value === value);
    return found ? found.label : value || '无';
  }

  async function fetchUserProfile(username) {
    const resp = await fetch(`/api/user-profile?username=${encodeURIComponent(username)}`);
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
    formatTime, maskName, statusLabel, orderStatusLabel, eventTypeMeta,
    EVIDENCE_TYPES, evidenceTypeLabel,
    HALL_FILTERS, isGoodCredit, isLowCredit, isWarningCredit,
    groupOrdersForHall, filterOrders,
    fetchCredit, fetchRatings, submitRating,
    fetchDisputes, fetchDisputeDetail, createDispute, checkDisputes,
    submitDisputeStatement, fetchUserProfile,
    fetchEvents,
    getCurrentUser, setCurrentUser
  };
})();
