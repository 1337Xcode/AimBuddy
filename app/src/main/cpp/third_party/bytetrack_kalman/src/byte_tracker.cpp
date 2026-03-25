#include "byte_tracker.h"
#include <fstream>

BYTETracker::BYTETracker(int max_time_lost, float track_high_thresh, float track_low_thresh, float new_track_thresh, float match_thresh)
{
	set_max_time_lost(max_time_lost);
	set_track_high_thresh(track_high_thresh);
	set_track_low_thresh(track_low_thresh);
	set_new_track_thresh(new_track_thresh);
	set_match_thresh(match_thresh);

	frame_id = 0;

	// Pre-size the 4-element tlbr scratch buffer (one allocation total)
	m_tlbrBuf.resize(4, 0.f);
	// Pre-reserve working vectors to avoid in-game reallocations.
	// Capacity is kept across frames; clear() does NOT free memory.
	m_activated.reserve(16); m_refind.reserve(16); m_removed.reserve(16);
	m_detections.reserve(32); m_detectionsLow.reserve(32); m_detectionsCp.reserve(32);
	m_trackedSwap.reserve(32); m_resa.reserve(32); m_resb.reserve(32);
	m_unconfirmed.reserve(32); m_tracked.reserve(32);
	m_pool.reserve(64); m_rTracked.reserve(32);
	m_uTrack.reserve(32); m_uDetection.reserve(32); m_uUnconfirmed.reserve(32);
}

BYTETracker::~BYTETracker()
{
}

void BYTETracker::update(const std::vector<Object>& objects, std::vector<STrack>& lost_stracks, std::vector<STrack>& output_stracks)
{
	////////////////// Step 1: Get detections //////////////////
	this->frame_id++;

	// Clear per-frame working vectors (retains allocated capacity — zero heap churn)
	m_activated.clear(); m_refind.clear(); m_removed.clear();
	m_detections.clear(); m_detectionsLow.clear(); m_detectionsCp.clear();
	m_trackedSwap.clear(); m_resa.clear(); m_resb.clear();
	m_unconfirmed.clear(); m_tracked.clear(); m_pool.clear(); m_rTracked.clear();
	m_dists.clear(); m_matches.clear();
	m_uTrack.clear(); m_uDetection.clear(); m_uUnconfirmed.clear();

	// CRITICAL: clear the persistent removed_stracks list every frame.
	// Once a track is removed it is already subtracted from lost_stracks — keeping it
	// in this->removed_stracks serves no purpose but causes O(n) scan growth every update.
	this->removed_stracks.clear();

	for (const auto& obj : objects) {
		m_tlbrBuf[0] = obj.x;
		m_tlbrBuf[1] = obj.y;
		m_tlbrBuf[2] = obj.x + obj.width;
		m_tlbrBuf[3] = obj.y + obj.height;
		STrack strack(STrack::tlbr_to_tlwh(m_tlbrBuf), obj.prob);
		if (obj.prob >= track_high_thresh)
			m_detections.push_back(strack);
		else if (obj.prob >= track_low_thresh)
			m_detectionsLow.push_back(strack);
	}

	// Partition this->tracked_stracks into confirmed / unconfirmed
	for (auto& st : this->tracked_stracks) {
		if (!st.is_activated) m_unconfirmed.push_back(&st);
		else                   m_tracked.push_back(&st);
	}

	////////////////// Step 2: First association, with IoU //////////////////
	m_pool = joint_stracks(m_tracked, this->lost_stracks);
	STrack::multi_predict(m_pool, this->kalman_filter);

	int dist_size = 0, dist_size_size = 0;
	m_dists = iou_distance(m_pool, m_detections, dist_size, dist_size_size);
	linear_assignment(m_dists, dist_size, dist_size_size, match_thresh, m_matches, m_uTrack, m_uDetection);

	for (const auto& m : m_matches) {
		STrack* track = m_pool[m[0]];
		STrack* det   = &m_detections[m[1]];
		if (track->state == TrackState::Tracked) { track->update(*det, this->frame_id); m_activated.push_back(*track); }
		else                                      { track->re_activate(*det, this->frame_id, false); m_refind.push_back(*track); }
	}

	////////////////// Step 3: Second association, using low score dets //////////////////
	for (int idx : m_uDetection)
		m_detectionsCp.push_back(m_detections[idx]);
	m_detections.clear();
	m_detections.assign(m_detectionsLow.begin(), m_detectionsLow.end());

	for (int idx : m_uTrack)
		if (m_pool[idx]->state == TrackState::Tracked)
			m_rTracked.push_back(m_pool[idx]);

	m_dists.clear();
	m_dists = iou_distance(m_rTracked, m_detections, dist_size, dist_size_size, false);
	m_matches.clear(); m_uTrack.clear(); m_uDetection.clear();
	linear_assignment(m_dists, dist_size, dist_size_size, 0.5, m_matches, m_uTrack, m_uDetection);

	for (const auto& m : m_matches) {
		STrack* track = m_rTracked[m[0]];
		STrack* det   = &m_detections[m[1]];
		if (track->state == TrackState::Tracked) { track->update(*det, this->frame_id); m_activated.push_back(*track); }
		else                                      { track->re_activate(*det, this->frame_id, false); m_refind.push_back(*track); }
	}
	for (int idx : m_uTrack) {
		STrack* track = m_rTracked[idx];
		if (track->state != TrackState::Lost) { track->mark_lost(); lost_stracks.push_back(*track); }
	}

	// Deal with unconfirmed tracks (tracks with only one beginning frame)
	m_detections.clear();
	m_detections.assign(m_detectionsCp.begin(), m_detectionsCp.end());
	m_dists.clear();
	m_dists = iou_distance(m_unconfirmed, m_detections, dist_size, dist_size_size);
	m_matches.clear(); m_uDetection.clear();
	linear_assignment(m_dists, dist_size, dist_size_size, 0.7, m_matches, m_uUnconfirmed, m_uDetection);

	for (const auto& m : m_matches) {
		m_unconfirmed[m[0]]->update(m_detections[m[1]], this->frame_id);
		m_activated.push_back(*m_unconfirmed[m[0]]);
	}
	for (int idx : m_uUnconfirmed) {
		m_unconfirmed[idx]->mark_removed();
		m_removed.push_back(*m_unconfirmed[idx]);
	}

	////////////////// Step 4: Init new stracks //////////////////
	for (int idx : m_uDetection) {
		STrack* t = &m_detections[idx];
		if (t->score < new_track_thresh) continue;
		t->activate(this->kalman_filter, this->frame_id);
		m_activated.push_back(*t);
	}

	////////////////// Step 5: Update state //////////////////
	for (auto& st : this->lost_stracks)
		if (this->frame_id - st.end_frame() > this->max_time_lost)
			{ st.mark_removed(); m_removed.push_back(st); }

	for (auto& st : this->tracked_stracks)
		if (st.state == TrackState::Tracked)
			m_trackedSwap.push_back(st);
	this->tracked_stracks.clear();
	this->tracked_stracks.assign(m_trackedSwap.begin(), m_trackedSwap.end());

	this->tracked_stracks = joint_stracks(this->tracked_stracks, m_activated);
	this->tracked_stracks = joint_stracks(this->tracked_stracks, m_refind);

	this->lost_stracks = sub_stracks(this->lost_stracks, this->tracked_stracks);
	for (const auto& st : lost_stracks)       // newly lost this frame
		this->lost_stracks.push_back(st);

	this->lost_stracks = sub_stracks(this->lost_stracks, this->removed_stracks);
	for (const auto& st : m_removed)
		this->removed_stracks.push_back(st);

	remove_duplicate_stracks(m_resa, m_resb, this->tracked_stracks, this->lost_stracks);
	this->tracked_stracks.clear();
	this->tracked_stracks.assign(m_resa.begin(), m_resa.end());
	this->lost_stracks.clear();
	this->lost_stracks.assign(m_resb.begin(), m_resb.end());

	output_stracks.clear();
	for (const auto& st : this->tracked_stracks)
		output_stracks.push_back(st);

	lost_stracks.clear();
	for (const auto& st : this->lost_stracks)
		lost_stracks.push_back(st);
}