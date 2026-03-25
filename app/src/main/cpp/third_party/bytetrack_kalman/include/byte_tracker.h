#ifndef BYTE_TRACKER_H
#define BYTE_TRACKER_H

#include "s_track.h"

struct Object
{
    float x, y, width, height;  // top-left origin, pixel coords
    int label;
    float prob;
};

class BYTETracker
{
public:
	BYTETracker(int max_time_lost = 15, float track_high_thresh = 0.5, float track_low_thresh = 0.1, float new_track_thresh = 0.6, float match_thresh = 0.8);
	~BYTETracker();

	void update(const std::vector<Object>& objects, std::vector<STrack>& lost_stracks, std::vector<STrack>& output_stracks);
	
	void set_max_time_lost(int val) { max_time_lost = val; };
	void set_track_high_thresh(float thresh) { track_high_thresh = thresh; };
	void set_track_low_thresh(float thresh) { track_low_thresh = thresh; };
	void set_new_track_thresh(float thresh) { new_track_thresh = thresh; };
	void set_match_thresh(float thresh) { match_thresh = thresh; };

private:
	std::vector<STrack*> joint_stracks(std::vector<STrack*> &tlista, std::vector<STrack> &tlistb);
	std::vector<STrack> joint_stracks(std::vector<STrack> &tlista, std::vector<STrack> &tlistb);

	std::vector<STrack> sub_stracks(std::vector<STrack> &tlista, std::vector<STrack> &tlistb);
	void remove_duplicate_stracks(std::vector<STrack> &resa, std::vector<STrack> &resb, std::vector<STrack> &stracksa, std::vector<STrack> &stracksb);

	void linear_assignment(std::vector<std::vector<float> > &cost_matrix, int cost_matrix_size, int cost_matrix_size_size, float thresh,
		std::vector<std::vector<int> > &matches, std::vector<int> &unmatched_a, std::vector<int> &unmatched_b);
	std::vector<std::vector<float> > iou_distance(std::vector<STrack*> &atracks, std::vector<STrack> &btracks, int &dist_size, int &dist_size_size, bool fuse_score = true);
	std::vector<std::vector<float> > iou_distance(std::vector<STrack> &atracks, std::vector<STrack> &btracks);
	std::vector<std::vector<float> > ious(std::vector<std::vector<float> > &atlbrs, std::vector<std::vector<float> > &btlbrs);

	double lapjv(const std::vector<std::vector<float> > &cost, std::vector<int> &rowsol, std::vector<int> &colsol, 
		bool extend_cost = false, float cost_limit = static_cast<float>(LONG_MAX), bool return_cost = true);

private:
	float track_high_thresh;
	float track_low_thresh;
	float new_track_thresh;
	float match_thresh;

	int frame_id;
	int max_time_lost;	// Number of frames allowable to go missing

	std::vector<STrack> tracked_stracks;
	std::vector<STrack> lost_stracks;
	std::vector<STrack> removed_stracks;
	byte_kalman::KalmanFilter kalman_filter;

	// ── Per-frame working storage — allocated once, reused every update() ──────
	// Eliminates ~14 vector heap allocations per frame (the dominant CPU cost).
	std::vector<STrack>  m_activated, m_refind, m_removed;
	std::vector<STrack>  m_detections, m_detectionsLow, m_detectionsCp;
	std::vector<STrack>  m_trackedSwap, m_resa, m_resb;
	std::vector<STrack*> m_unconfirmed, m_tracked, m_pool, m_rTracked;
	std::vector<std::vector<float>> m_dists;
	std::vector<std::vector<int>>   m_matches;
	std::vector<int>     m_uTrack, m_uDetection, m_uUnconfirmed;
	std::vector<float>   m_tlbrBuf; // 4-element buffer reused for tlbr_to_tlwh
};

#endif // BYTE_TRACKER_H